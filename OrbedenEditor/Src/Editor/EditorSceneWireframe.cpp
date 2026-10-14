#include "Editor/EditorSceneWireframe.h"

#include "Log/Log.h"
#include "Rendering/RenderMath.h"
#include "Rendering/RenderSystem.h"
#include "Rendering/SceneCuller.h"
#include "Runtime/Particles/ParticleSimulationSystem.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr uint32 MaximumVertices = 262143u;

    constexpr const char* VertexSource = R"(#version 430 core
layout(location = 0) in vec3 a_Position;
uniform mat4 u_ViewProjection;
void main()
{
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}
)";

    constexpr const char* FragmentSource = R"(#version 430 core
out vec4 FragColor;
void main()
{
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

    //保存并还原编辑器独立绘制使用的 OpenGL 状态
    struct SavedOpenGLState
    {
        GLint drawFramebuffer, readFramebuffer, viewport[4], scissor[4], shader, vertexArray, arrayBuffer, elementBuffer;
        GLint activeTexture, texture, depthFunction, cullMode, blendSourceRgb, blendDestinationRgb;
        GLint blendSourceAlpha, blendDestinationAlpha, blendEquationRgb, blendEquationAlpha, unpackAlignment;
        GLint polygonMode[2];
        GLfloat clearColor[4], polygonFactor, polygonUnits, lineWidth;
        GLboolean depth, depthWrite, blend, cull, scissorEnabled, polygonOffset, framebufferSrgb, colorWrite[4];
        GLboolean lineSmooth, multisample, polygonOffsetLine;

        //读取绘制状态
        SavedOpenGLState()
        {
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
            glGetIntegerv(GL_VIEWPORT, viewport);
            glGetIntegerv(GL_SCISSOR_BOX, scissor);
            glGetIntegerv(GL_CURRENT_PROGRAM, &shader);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
            glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
            glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
            glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
            glGetIntegerv(GL_BLEND_SRC_RGB, &blendSourceRgb);
            glGetIntegerv(GL_BLEND_DST_RGB, &blendDestinationRgb);
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSourceAlpha);
            glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDestinationAlpha);
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb);
            glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
            glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
            glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &polygonFactor);
            glGetFloatv(GL_POLYGON_OFFSET_UNITS, &polygonUnits);
            glGetIntegerv(GL_POLYGON_MODE, polygonMode);
            glGetFloatv(GL_LINE_WIDTH, &lineWidth);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
            glGetBooleanv(GL_COLOR_WRITEMASK, colorWrite);
            depth = glIsEnabled(GL_DEPTH_TEST);
            blend = glIsEnabled(GL_BLEND);
            cull = glIsEnabled(GL_CULL_FACE);
            scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
            polygonOffset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
            framebufferSrgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
            lineSmooth = glIsEnabled(GL_LINE_SMOOTH);
            multisample = glIsEnabled(GL_MULTISAMPLE);
            polygonOffsetLine = glIsEnabled(GL_POLYGON_OFFSET_LINE);
        }

        //恢复绘制状态
        ~SavedOpenGLState()
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
            glUseProgram(shader);
            glBindVertexArray(vertexArray);
            if (vertexArray != 0) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementBuffer);
            glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glActiveTexture(activeTexture);
            glDepthFunc(depthFunction);
            glDepthMask(depthWrite);
            glColorMask(colorWrite[0], colorWrite[1], colorWrite[2], colorWrite[3]);
            glCullFace(cullMode);
            glBlendFuncSeparate(blendSourceRgb, blendDestinationRgb, blendSourceAlpha, blendDestinationAlpha);
            glBlendEquationSeparate(blendEquationRgb, blendEquationAlpha);
            glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
            glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
            glPolygonOffset(polygonFactor, polygonUnits);
            glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);
            glLineWidth(lineWidth);
            const GLenum states[] = { GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST,
                GL_POLYGON_OFFSET_FILL, GL_FRAMEBUFFER_SRGB, GL_LINE_SMOOTH, GL_MULTISAMPLE, GL_POLYGON_OFFSET_LINE };
            const GLboolean values[] = { depth, blend, cull, scissorEnabled, polygonOffset, framebufferSrgb,
                lineSmooth, multisample, polygonOffsetLine };
            for (usize index = 0; index < std::size(states); ++index)
                if (values[index]) glEnable(states[index]); else glDisable(states[index]);
        }
    };
}

//创建硬编码 Shader 和统一绘制缓冲
bool EditorSceneWireframe::PrepareResources(IWindow* window)
{
    if (initialized) return program.IsValid() && vertexInput.IsValid();
    if (!backend.Initialize(window)) return false;
    resources.Initialize(&backend);
    output.Initialize(&backend);
    initialized = true;

    GpuShaderProgramDesc shader;
    shader.vertexSource = VertexSource;
    shader.fragmentSource = FragmentSource;
    program = backend.CreateShaderProgram(shader);
    GpuBufferDesc buffer;
    buffer.size = MaximumVertices * sizeof(vector3);
    buffer.usage = GpuBufferUsage::Stream;
    vertexBuffer = backend.CreateVertexBuffer(buffer);
    List<uint32> indices(MaximumVertices);
    for (uint32 index = 0; index < MaximumVertices; ++index) indices[index] = index;
    buffer.data = indices.data();
    buffer.size = indices.size() * sizeof(uint32);
    buffer.usage = GpuBufferUsage::Static;
    indexBuffer = backend.CreateIndexBuffer(buffer);
    GpuVertexInputDesc input;
    input.vertexBuffer = vertexBuffer;
    input.indexBuffer = indexBuffer;
    input.stride = sizeof(vector3);
    vertexInput = backend.CreateVertexInput(input);
    backend.BindVertexInput(vertexInput);
    for (uint32 index = 1; index < 4; ++index) glDisableVertexAttribArray(index);
    backend.BindVertexInput({});
    vertices.reserve(MaximumVertices);
    if (!program.IsValid() || !vertexInput.IsValid())
        Log::Error("Editor wireframe setup failed: shader or geometry buffers are unavailable.");
    return program.IsValid() && vertexInput.IsValid();
}

//提交一块统一线框几何
void EditorSceneWireframe::ExecuteBatch()
{
    if (vertices.empty()) return;
    if (backend.UploadVertexBuffer(vertexBuffer, vertices.data(), vertices.size() * sizeof(vector3),
        MaximumVertices * sizeof(vector3)))
        backend.DrawIndexed(0, static_cast<uint32>(vertices.size()));
    vertices.clear();
}

//追加一个世界空间三角形
void EditorSceneWireframe::AppendTriangle(const vector3& first, const vector3& second, const vector3& third)
{
    for (const vector3& vertex : { first, second, third })
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z)) return;
    if (vertices.size() + 3 > MaximumVertices) ExecuteBatch();
    vertices.push_back(first);
    vertices.push_back(second);
    vertices.push_back(third);
}

//追加一组世界空间三角形
void EditorSceneWireframe::AppendMesh(const Mesh& mesh, uint32 indexStart, uint32 indexCount, const matrix4x4& model)
{
    usize start = indexStart;
    if (start > mesh.indices.size() || indexCount > mesh.indices.size() - start) return;
    for (usize index = start; index + 2 < start + indexCount; index += 3)
    {
        uint32 first = mesh.indices[index], second = mesh.indices[index + 1], third = mesh.indices[index + 2];
        if (first >= mesh.vertices.size() || second >= mesh.vertices.size() || third >= mesh.vertices.size()) continue;
        AppendTriangle(RenderMath::TransformPoint(model, mesh.vertices[first]),
            RenderMath::TransformPoint(model, mesh.vertices[second]), RenderMath::TransformPoint(model, mesh.vertices[third]));
    }
}

//获取描边专用的网格副本
Mesh* EditorSceneWireframe::GetOutlineMesh(Mesh* source)
{
    if (!source) return nullptr;
    OutlineMesh& cached = outlineMeshes[source->GetObjectId()];
    if (cached.source.Get() != source)
    {
        cached.source.Set(source);
        cached.version = 0;
    }
    Mesh* copy = cached.copy.Get();
    if (!copy)
    {
        copy = Object::CreateInstance<Mesh>();
        cached.copy.Set(copy);
        cached.source.Set(source);
    }
    if (!copy) return nullptr;
    if (cached.version != source->GetContentVersion())
    {
        copy->vertices = source->vertices;
        copy->indices = source->indices;
        copy->subMeshes = source->subMeshes;
        copy->MarkDirty();
        cached.version = source->GetContentVersion();
    }
    return copy;
}

//设置独立线框画面的选中描边
void EditorSceneWireframe::SetHighlights(const List<EditorSelectionOutline::SelectionHighlight>& highlights)
{
    selectionHighlights = highlights;
    outline.SetHighlights(highlights);
}

//绘制独立线框或叠加线框并返回显示纹理
GpuTextureID EditorSceneWireframe::Render(IWindow* window, World& world, RenderSystem& renderSystem,
    RenderCamera& camera, bool overlay)
{
    VisibleSet visible;
    SceneCuller culler;
    const RenderScene& scene = renderSystem.GetCurrentScene();
    culler.Cull(scene, camera, visible);
    scene.BuildRenderItems(visible);
    {
        SavedOpenGLState saved;
        if (!PrepareResources(window)) return {};
        //清理失效网格的描边副本
        for (auto entry = outlineMeshes.begin(); entry != outlineMeshes.end();)
        {
            if (entry->second.source.Get()) { ++entry; continue; }
            if (Mesh* copy = entry->second.copy.Get()) Object::DestroyObjectFromBinding(copy);
            entry = outlineMeshes.erase(entry);
        }
        resources.ReleaseDestroyedResources();
        //重设独立后端的状态与绑定
        backend.BindShaderProgram({});
        backend.BindVertexInput({});
        glActiveTexture(GL_TEXTURE0);
        backend.BindTexture(0, {});
        backend.SetDepthTest(false); backend.SetDepthTest(true);
        backend.SetDepthWrite(false); backend.SetDepthWrite(true);
        backend.SetBlend(false); backend.SetBlend(true);
        backend.SetBlendMode(BlendMode::Additive); backend.SetBlendMode(BlendMode::Alpha);
        backend.SetCullMode(CullMode::Back); backend.SetCullMode(CullMode::None);
        backend.SetDepthCompare(DepthCompare::LessEqual); backend.SetDepthCompare(DepthCompare::Less);
        backend.SetPolygonOffset(true, 0.0f, 0.0f); backend.SetPolygonOffset(false, 0.0f, 0.0f);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        //创建独立线框的场景与显示目标
        if (!overlay)
        {
            if (width != camera.viewportWidth || height != camera.viewportHeight
                || !sceneTarget.IsValid() || !displayTarget.IsValid() || !depthTexture.IsValid())
            {
                if (sceneTarget.IsValid()) backend.DeleteRenderTarget(sceneTarget);
                if (displayTarget.IsValid()) backend.DeleteRenderTarget(displayTarget);
                if (depthTexture.IsValid()) backend.DeleteDepthTexture(depthTexture);
                width = camera.viewportWidth;
                height = camera.viewportHeight;
                GpuDepthTextureDesc depth;
                depth.width = width; depth.height = height;
                depthTexture = backend.CreateDepthTexture(depth);
                GpuRenderTargetDesc target;
                target.width = width; target.height = height;
                target.depthTexture = depthTexture;
                target.format = GpuRenderTargetFormat::RGBA16F;
                sceneTarget = backend.CreateRenderTarget(target);
                target.depthTexture = {};
                target.colorOnly = true;
                target.format = GpuRenderTargetFormat::RGBA8;
                displayTarget = backend.CreateRenderTarget(target);
            }
            if (!sceneTarget.IsValid() || !displayTarget.IsValid()) return {};
            camera.renderTarget = sceneTarget;
            camera.sceneDepthTexture = depthTexture;
            camera.sceneColorTexture = backend.GetRenderTargetColorTexture(sceneTarget);
            camera.finalRenderTarget = displayTarget;
        }

        //绘制统一线框材质
        RenderPassDesc pass;
        pass.renderTarget = camera.renderTarget;
        pass.width = camera.viewportWidth; pass.height = camera.viewportHeight;
        pass.clearMode = overlay ? ClearMode::None : ClearMode::SolidColor;
        pass.clearColor = camera.clearColor;
        backend.BeginPass(pass);
        backend.SetDepthWrite(false);
        backend.SetDepthTest(overlay);
        backend.SetDepthCompare(DepthCompare::LessEqual);
        //启用固定一像素的线段光栅化
        backend.SetBlend(false);
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
        backend.BindShaderProgram(program);
        backend.SetUniformMatrix4("u_ViewProjection", camera.viewProjectionMatrix);
        backend.BindVertexInput(vertexInput);
        vertices.clear();
        for (const RenderItem& item : visible.renderItems)
            if (item.mesh) AppendMesh(*item.mesh, item.indexStart, item.indexCount, item.localToWorld);

        //追加显式实例几何
        for (const InstanceSubmission& submission : renderSystem.GetActiveInstanceSubmissions())
        {
            Mesh* mesh = submission.mesh.Get();
            if (!mesh || !submission.material.Get() || submission.world != &world
                || submission.contentRevision != world.GetContentRevision()
                || (!submission.options.camera.IsNull() && submission.options.camera != camera.ens)
                || (submission.options.drawLayer & camera.drawLayerMask) == 0
                || submission.subMeshIndex >= mesh->subMeshes.size()) continue;
            const SubMesh& subMesh = mesh->subMeshes[submission.subMeshIndex];
            for (const MeshInstanceData& instance : submission.instances)
            {
                matrix4x4 model = RenderMath::TRS(instance.position, instance.rotation, instance.scale);
                if (RenderMath::Intersects(camera.viewFrustum, RenderMath::TransformBounds(model, mesh->GetLocalBounds())))
                    AppendMesh(*mesh, subMesh.indexStart, subMesh.indexCount, model);
            }
        }

        //追加粒子与拖尾几何
        if (ParticleSimulationSystem* simulation = ParticleSimulationSystem::Current())
        {
            ParticleFrameSnapshot snapshot;
            bool preview = simulation->HasPreviewContent() && !world.IsRuntimeActive();
            particles.ClearCameraScratch();
            particles.CaptureFrame(world, simulation->GetRenderContext(preview), simulation->GetTransformCache(), snapshot);
            List<DrawItem> items;
            particles.AppendCameraItems(snapshot, camera, items);
            DrawBatch quads;
            for (uint32 index = 0; index < items.size(); ++index)
            {
                const DrawItem& item = items[index];
                if (item.geometry == DrawGeometry::Mesh)
                {
                    if (item.mesh) AppendMesh(*item.mesh, item.indexStart, item.indexCount, item.model);
                }
                else quads.items.push_back(index);
            }
            List<ExpandedGeometryChunk> chunks;
            particles.ExpandBatch(quads, items, chunks, resources);
            for (const ExpandedGeometryChunk& chunk : chunks)
                for (usize index = 0; index + 2 < chunk.indices.size(); index += 3)
                {
                    const auto& a = chunk.vertices[chunk.indices[index]].position;
                    const auto& b = chunk.vertices[chunk.indices[index + 1]].position;
                    const auto& c = chunk.vertices[chunk.indices[index + 2]].position;
                    AppendTriangle({ a[0], a[1], a[2] }, { b[0], b[1], b[2] }, { c[0], c[1], c[2] });
                }
        }
        ExecuteBatch();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_POLYGON_OFFSET_LINE);
        backend.SetPolygonOffset(false, 0.0f, 0.0f);
        backend.EndPass();

        //绘制独立画面的选择描边
        if (!overlay)
        {
            visible.renderItems.erase(std::remove_if(visible.renderItems.begin(), visible.renderItems.end(),
                [this](const RenderItem& item)
                {
                    return std::none_of(selectionHighlights.begin(), selectionHighlights.end(),
                        [&item](const auto& highlight) { return highlight.ens == item.ens; });
                }), visible.renderItems.end());
            for (RenderItem& item : visible.renderItems) item.mesh = GetOutlineMesh(item.mesh);
            resources.ReleaseDestroyedResources();
            outline.Render(backend, resources, camera, visible);
        }
    }
    if (overlay) return {};

    //绘制原有 UI 并转换显示颜色
    renderSystem.GetUIRenderer().RenderWorldSpace(camera);
    renderSystem.GetUIRenderer().RenderEditorPreview(camera);
    SavedOpenGLState saved;
    backend.BindShaderProgram({});
    backend.BindVertexInput({});
    backend.BindTexture(0, {});
    backend.SetDepthTest(true); backend.SetDepthTest(false);
    backend.SetDepthWrite(true); backend.SetDepthWrite(false);
    backend.SetBlend(true); backend.SetBlend(false);
    backend.SetCullMode(CullMode::Back); backend.SetCullMode(CullMode::None);
    OutputPass::Parameters parameters;
    parameters.sourceTexture = camera.sceneColorTexture;
    parameters.destinationRenderTarget = displayTarget;
    parameters.width = width; parameters.height = height;
    parameters.exposure = camera.exposure;
    if (!output.Render(parameters)) return {};
    return backend.GetRenderTargetColorTexture(displayTarget);
}

//释放编辑器线框资源
void EditorSceneWireframe::ReleaseResources()
{
    if (!initialized) return;
    SavedOpenGLState saved;
    output.Shutdown();
    outline.ReleaseResources(backend);
    resources.Shutdown();
    for (auto& entry : outlineMeshes)
        if (Mesh* copy = entry.second.copy.Get()) Object::DestroyObjectFromBinding(copy);
    outlineMeshes.clear();
    if (vertexInput.IsValid()) backend.DeleteVertexInput(vertexInput);
    if (vertexBuffer.IsValid()) backend.DeleteVertexBuffer(vertexBuffer);
    if (indexBuffer.IsValid()) backend.DeleteIndexBuffer(indexBuffer);
    if (program.IsValid()) backend.DeleteShaderProgram(program);
    if (sceneTarget.IsValid()) backend.DeleteRenderTarget(sceneTarget);
    if (displayTarget.IsValid()) backend.DeleteRenderTarget(displayTarget);
    if (depthTexture.IsValid()) backend.DeleteDepthTexture(depthTexture);
    backend.Shutdown();
    program = {}; vertexInput = {}; vertexBuffer = {}; indexBuffer = {};
    sceneTarget = {}; displayTarget = {}; depthTexture = {};
    width = 0; height = 0;
    initialized = false;
    vertices.clear();
}
