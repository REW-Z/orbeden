#include "Rendering/DebugLineRenderer.h"

//绑定渲染后端
void DebugLineRenderer::Initialize(RenderBackend* value)
{
    Shutdown();
    backend = value;
}

//释放调试绘制资源
void DebugLineRenderer::Shutdown()
{
    if (backend)
    {
        backend->DeleteVertexInput(input);
        backend->DeleteVertexBuffer(vertices);
        backend->DeleteIndexBuffer(indices);
        backend->DeleteShaderProgram(program);
    }
    backend = nullptr;
    input = {};
    vertices = {};
    indices = {};
    program = {};
    capacity = 0;
}

//绘制相机可见的线条
void DebugLineRenderer::Render(const List<DebugLine>& lines, const matrix4x4& viewProjection, uint32 layerMask)
{
    if (!backend || lines.empty()) return;
    if (!program.IsValid())
    {
        GpuShaderProgramDesc shader;
        shader.vertexSource = "#version 430 core\nlayout(location=0) in vec3 a_Position; uniform mat4 u_ViewProjection; void main(){gl_Position=u_ViewProjection*vec4(a_Position,1.0);}";
        shader.fragmentSource = "#version 430 core\nuniform vec4 u_Color; out vec4 FragColor; void main(){FragColor=u_Color;}";
        program = backend->CreateShaderProgram(shader);
    }
    if (!program.IsValid()) return;

    //准备线条顶点与连续索引
    List<vector3> positions;
    positions.reserve(lines.size() * 2);
    for (const DebugLine& line : lines)
    {
        positions.push_back(line.start);
        positions.push_back(line.end);
    }
    if (positions.size() > capacity)
    {
        backend->DeleteVertexInput(input);
        backend->DeleteVertexBuffer(vertices);
        backend->DeleteIndexBuffer(indices);
        capacity = positions.size();
        List<uint32> sequence(capacity);
        for (uint32 index = 0; index < sequence.size(); ++index) sequence[index] = index;
        vertices = backend->CreateVertexBuffer({ nullptr, capacity * sizeof(vector3), GpuBufferUsage::Stream });
        indices = backend->CreateIndexBuffer({ sequence.data(), sequence.size() * sizeof(uint32) });
        input = backend->CreateVertexInput({ vertices, indices, sizeof(vector3), GpuVertexLayout::Position });
    }
    if (!input.IsValid() || !backend->UploadVertexBuffer(vertices, positions.data(), positions.size() * sizeof(vector3), capacity * sizeof(vector3))) return;

    //提交线条参数与图元
    PipelineState saved = backend->GetPipelineState();
    PipelineState& state = backend->GetPipelineState();
    state = {};
    state.depthWrite = false;
    state.blend = true;
    state.topology = PrimitiveTopology::Lines;
    backend->BindShaderProgram(program);
    backend->BindVertexInput(input);
    backend->GetParameters().SetMatrix4("u_ViewProjection", viewProjection);
    for (uint32 index = 0; index < lines.size(); ++index)
    {
        const DebugLine& line = lines[index];
        if ((line.drawLayer & layerMask) == 0) continue;
        state.depthTest = line.depthTest;
        backend->GetParameters().SetColor("u_Color", line.tint);
        backend->DrawIndexed(index * 2, 2);
    }
    state = saved;
    backend->BindVertexInput({});
    backend->BindShaderProgram({});
}
