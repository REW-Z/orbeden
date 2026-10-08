#include "Rendering/FullscreenQuad.h"

#include "Rendering/Backend/RenderBackend.h"

const char* const FullscreenQuadVertexSource =
    "#version 430 core\n"
    "layout(location=0) in vec3 a_Position;\n"
    "out vec2 v_Uv;\n"
    "void main(){v_Uv=a_Position.xy*0.5+0.5;gl_Position=vec4(a_Position.xy,0.0,1.0);}\n";

//绑定后端，清空可能残留的旧句柄
void FullscreenQuad::Initialize(RenderBackend* renderBackend)
{
    if (backend == renderBackend) return;

    Shutdown();
    backend = renderBackend;
}

void FullscreenQuad::Shutdown()
{
    if (!backend)
    {
        vertexInput = GpuVertexInputID();
        vertexBuffer = GpuVertexBufferID();
        indexBuffer = GpuIndexBufferID();
        return;
    }

    if (vertexInput.IsValid()) backend->DeleteVertexInput(vertexInput);
    if (vertexBuffer.IsValid()) backend->DeleteVertexBuffer(vertexBuffer);
    if (indexBuffer.IsValid()) backend->DeleteIndexBuffer(indexBuffer);
    vertexInput = GpuVertexInputID();
    vertexBuffer = GpuVertexBufferID();
    indexBuffer = GpuIndexBufferID();
}

bool FullscreenQuad::EnsureReady()
{
    if (vertexInput.IsValid()) return true;
    if (!backend) return false;

    constexpr int32 VertexFloatCount = 11;
    constexpr float32 Corners[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
    constexpr uint32 Indices[6] = { 0, 1, 2, 0, 2, 3 };

    float32 vertexData[4 * VertexFloatCount] = {};
    for (int32 index = 0; index < 4; ++index)
    {
        vertexData[index * VertexFloatCount + 0] = Corners[index][0];
        vertexData[index * VertexFloatCount + 1] = Corners[index][1];
    }

    GpuBufferDesc vertexBufferDesc;
    vertexBufferDesc.data = vertexData;
    vertexBufferDesc.size = sizeof(vertexData);
    vertexBuffer = backend->CreateVertexBuffer(vertexBufferDesc);

    GpuBufferDesc indexBufferDesc;
    indexBufferDesc.data = Indices;
    indexBufferDesc.size = sizeof(Indices);
    indexBuffer = backend->CreateIndexBuffer(indexBufferDesc);

    if (vertexBuffer.IsValid() && indexBuffer.IsValid())
    {
        GpuVertexInputDesc inputDesc;
        inputDesc.vertexBuffer = vertexBuffer;
        inputDesc.indexBuffer = indexBuffer;
        inputDesc.stride = sizeof(float32) * VertexFloatCount;
        vertexInput = backend->CreateVertexInput(inputDesc);
    }

    //创建失败时把半成品清干净，下次调用重新尝试
    if (!vertexInput.IsValid()) Shutdown();
    return vertexInput.IsValid();
}
