#include "Editor/EditorSceneWireframe.h"

#include "Log/Log.h"
#include "Profiler/Profiler.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/RenderMath.h"
#include "Runtime/World.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr const char* VertexSource = R"(#version 430 core
layout(location = 0) in vec3 a_Position;
uniform mat4 u_Model;
uniform mat4 u_ViewProjection;
void main()
{
    gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
}
)";

    constexpr const char* InstancedVertexSource = R"(#version 430 core
layout(location = 0) in vec3 a_Position;
layout(location = 5) in mat4 a_Model;
uniform mat4 u_ViewProjection;
void main()
{
    gl_Position = u_ViewProjection * a_Model * vec4(a_Position, 1.0);
}
)";

    constexpr const char* FragmentSource = R"(#version 430 core
out vec4 FragColor;
void main()
{
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

    //保存并恢复后端未缓存的光栅化状态
    struct SavedRasterState
    {
        GLint polygonMode[2];
        GLfloat lineWidth, polygonFactor, polygonUnits;
        GLboolean lineSmooth, multisample, polygonOffsetLine;

        //读取线框光栅化状态
        SavedRasterState()
        {
            glGetIntegerv(GL_POLYGON_MODE, polygonMode);
            glGetFloatv(GL_LINE_WIDTH, &lineWidth);
            glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &polygonFactor);
            glGetFloatv(GL_POLYGON_OFFSET_UNITS, &polygonUnits);
            lineSmooth = glIsEnabled(GL_LINE_SMOOTH);
            multisample = glIsEnabled(GL_MULTISAMPLE);
            polygonOffsetLine = glIsEnabled(GL_POLYGON_OFFSET_LINE);
        }

        //恢复线框光栅化状态
        ~SavedRasterState()
        {
            glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);
            glLineWidth(lineWidth);
            glPolygonOffset(polygonFactor, polygonUnits);
            if (lineSmooth) glEnable(GL_LINE_SMOOTH); else glDisable(GL_LINE_SMOOTH);
            if (multisample) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
            if (polygonOffsetLine) glEnable(GL_POLYGON_OFFSET_LINE); else glDisable(GL_POLYGON_OFFSET_LINE);
        }
    };
}

//创建线框 Shader
bool EditorSceneWireframe::PrepareResources(RenderBackend& renderBackend)
{
    if (backend) return program.IsValid()
        && (!backend->SupportsInstancing() || instancedProgram.IsValid());
    backend = &renderBackend;

    GpuShaderProgramDesc shader;
    shader.vertexSource = VertexSource;
    shader.fragmentSource = FragmentSource;
    program = backend->CreateShaderProgram(shader);
    if (backend->SupportsInstancing())
    {
        shader.vertexSource = InstancedVertexSource;
        instancedProgram = backend->CreateShaderProgram(shader);
    }
    const bool ready = program.IsValid()
        && (!backend->SupportsInstancing() || instancedProgram.IsValid());
    if (!ready) Log::Error("Editor wireframe setup failed: shader is unavailable.");
    return ready;
}

//追加一个网格绘制项
void EditorSceneWireframe::AppendMeshDraw(Mesh* mesh, uint32 indexStart, uint32 indexCount,
    const matrix4x4& model)
{
    if (!mesh || indexCount == 0 || indexStart > mesh->indices.size()
        || indexCount > mesh->indices.size() - indexStart) return;
    for (float32 element : model.m)
        if (!std::isfinite(element)) return;
    draws.push_back({ mesh, indexStart, indexCount, model });
}

//提交同一子网格的实例批次
void EditorSceneWireframe::DrawInstances(const GpuMesh& mesh, uint32 indexStart, uint32 indexCount,
    std::span<const MeshDraw> batch)
{
    backend->BindShaderProgram(instancedProgram);
    backend->BindVertexInput(mesh.instancedVertexInput);
    for (usize start = 0; start < batch.size(); start += MaximumBatchInstances)
    {
        usize count = std::min(batch.size() - start, static_cast<usize>(MaximumBatchInstances));
        instances.clear();
        for (const MeshDraw& draw : batch.subspan(start, count))
        {
            GpuMeshInstance& instance = instances.emplace_back();
            std::copy(std::begin(draw.model.m), std::end(draw.model.m), instance.model);
        }
        if (!instanceBuffer.IsValid())
        {
            instanceCapacity = 64u;
            GpuBufferDesc buffer;
            buffer.size = instanceCapacity * sizeof(GpuMeshInstance);
            buffer.usage = GpuBufferUsage::Stream;
            instanceBuffer = backend->CreateVertexBuffer(buffer);
        }
        if (!instanceBuffer.IsValid()) return;
        while (instanceCapacity < count) instanceCapacity *= 2;
        if (backend->UploadVertexBuffer(instanceBuffer, instances.data(), count * sizeof(GpuMeshInstance),
            instanceCapacity * sizeof(GpuMeshInstance)) && backend->BindInstanceBuffer(instanceBuffer, 0))
            backend->DrawIndexedInstanced(indexStart, indexCount, static_cast<uint32>(count));
    }
}

//在相机场景缓冲绘制网格线框
void EditorSceneWireframe::Render(RenderBackend& renderBackend, GpuResourceManager& resources,
    World& world, const RenderCamera& camera, const VisibleSet& visibleSet,
    std::span<const InstanceSubmission> submissions, bool overlay)
{
    PROFILE("Editor/Wireframe");
    RenderPassDesc pass;
    pass.renderTarget = camera.renderTarget;
    pass.width = camera.viewportWidth;
    pass.height = camera.viewportHeight;
    pass.clearMode = overlay ? ClearMode::None : camera.clearMode;
    pass.clearColor = camera.clearColor;
    renderBackend.SetDepthWrite(true);
    renderBackend.BeginPass(pass);
    if (!PrepareResources(renderBackend))
    {
        renderBackend.EndPass();
        return;
    }

    //收集静态网格与显式实例
    draws.clear();
    for (const RenderItem& item : visibleSet.renderItems)
        AppendMeshDraw(item.mesh, item.indexStart, item.indexCount, item.localToWorld);
    for (const InstanceSubmission& submission : submissions)
    {
        Mesh* mesh = submission.mesh.Get();
        if (!mesh || !submission.material.Get() || submission.world != &world
            || submission.contentRevision != world.GetContentRevision()
            || (!submission.options.camera.IsNull() && submission.options.camera != camera.ens)
            || (submission.options.drawLayer & camera.drawLayerMask) == 0
            || submission.subMeshIndex >= mesh->subMeshes.size()) continue;
        const SubMesh& subMesh = mesh->subMeshes[submission.subMeshIndex];
        const bounds3 localBounds = mesh->GetLocalBounds();
        for (const MeshInstanceData& instance : submission.instances)
        {
            matrix4x4 model = RenderMath::TRS(instance.position, instance.rotation, instance.scale);
            if (RenderMath::Intersects(camera.viewFrustum, RenderMath::TransformBounds(model, localBounds)))
                AppendMeshDraw(mesh, subMesh.indexStart, subMesh.indexCount, model);
        }
    }
    std::sort(draws.begin(), draws.end(), [](const MeshDraw& first, const MeshDraw& second)
    {
        if (first.mesh != second.mesh) return first.mesh->GetObjectId() < second.mesh->GetObjectId();
        if (first.indexStart != second.indexStart) return first.indexStart < second.indexStart;
        return first.indexCount < second.indexCount;
    });

    //绘制一像素三角形边线
    SavedRasterState saved;
    renderBackend.SetDepthWrite(false);
    renderBackend.SetDepthTest(overlay);
    renderBackend.SetDepthCompare(DepthCompare::LessEqual);
    renderBackend.SetBlend(false);
    renderBackend.SetCullMode(CullMode::None);
    renderBackend.SetPolygonOffset(false, 0.0f, 0.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(1.0f);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_MULTISAMPLE);
    if (overlay)
    {
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.0f, -2.0f);
    }
    else glDisable(GL_POLYGON_OFFSET_LINE);
    renderBackend.BindShaderProgram(program);
    renderBackend.SetUniformMatrix4("u_ViewProjection", camera.viewProjectionMatrix);
    if (instancedProgram.IsValid())
    {
        renderBackend.BindShaderProgram(instancedProgram);
        renderBackend.SetUniformMatrix4("u_ViewProjection", camera.viewProjectionMatrix);
    }
    for (usize start = 0; start < draws.size();)
    {
        const MeshDraw& draw = draws[start];
        usize end = start + 1;
        while (end < draws.size() && draws[end].mesh == draw.mesh
            && draws[end].indexStart == draw.indexStart && draws[end].indexCount == draw.indexCount) ++end;
        const GpuMesh* mesh = resources.GetMesh(draw.mesh);
        if (mesh && mesh->IsValid())
        {
            if (end - start > 1 && instancedProgram.IsValid() && mesh->instancedVertexInput.IsValid())
                DrawInstances(*mesh, draw.indexStart, draw.indexCount,
                    std::span<const MeshDraw>(draws).subspan(start, end - start));
            else
            {
                renderBackend.BindShaderProgram(program);
                renderBackend.BindVertexInput(mesh->vertexInput);
                for (usize index = start; index < end; ++index)
                {
                    renderBackend.SetUniformMatrix4("u_Model", draws[index].model);
                    renderBackend.DrawIndexed(draw.indexStart, draw.indexCount);
                }
            }
        }
        start = end;
    }

    //恢复后续相机绘制使用的状态
    renderBackend.BindVertexInput({});
    renderBackend.SetDepthWrite(true);
    renderBackend.SetDepthTest(true);
    renderBackend.SetDepthCompare(DepthCompare::Less);
    renderBackend.SetCullMode(CullMode::Back);
    renderBackend.EndPass();
}

//释放线框 Shader、实例缓冲和临时数据
void EditorSceneWireframe::ReleaseResources()
{
    if (backend)
    {
        backend->DeleteShaderProgram(program);
        backend->DeleteShaderProgram(instancedProgram);
        backend->DeleteVertexBuffer(instanceBuffer);
    }
    backend = nullptr;
    program = {};
    instancedProgram = {};
    instanceBuffer = {};
    instanceCapacity = 0;
    List<MeshDraw>().swap(draws);
    List<GpuMeshInstance>().swap(instances);
}
