#include "Rendering/ImGuiRenderer.h"

#include "Log/Log.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{
    constexpr const char* VertexSource = R"(#version 430 core
layout(location=0) in vec2 a_Position;
layout(location=1) in vec2 a_Uv;
layout(location=2) in vec4 a_Color;
uniform mat4 u_Projection;
out vec2 v_Uv;
out vec4 v_Color;
void main(){v_Uv=a_Uv;v_Color=a_Color;gl_Position=u_Projection*vec4(a_Position,0.0,1.0);}
)";
    constexpr const char* FragmentSource = R"(#version 430 core
in vec2 v_Uv;
in vec4 v_Color;
uniform sampler2D u_Texture;
out vec4 FragColor;
void main(){FragColor=v_Color*texture(u_Texture,v_Uv);}
)";
}

//绑定当前 ImGui 上下文与渲染后端
bool ImGuiRenderer::Initialize(RenderBackend& value)
{
    if (backend) return backend == &value;
    context = ImGui::GetCurrentContext();
    if (!context) return false;
    backend = &value;
    program = backend->CreateShaderProgram({ VertexSource, FragmentSource });
    if (!program.IsValid()) { backend = nullptr; context = nullptr; return false; }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "Orbeden_RenderBackend";
    io.BackendRendererUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Renderer_TextureMaxWidth = platform.Renderer_TextureMaxHeight = RenderBackend::GetMaxTextureSize();
    return true;
}

//释放上下文持有的渲染资源
void ImGuiRenderer::Shutdown()
{
    if (!backend) return;
    ImGui::SetCurrentContext(context);
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
        if (texture->RefCount == 1 && texture->BackendUserData == backend)
        {
            backend->DeleteTexture({ static_cast<uint32>(texture->TexID) });
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
            texture->BackendUserData = nullptr;
        }
    backend->DeleteVertexInput(input);
    backend->DeleteVertexBuffer(vertices);
    backend->DeleteIndexBuffer(indices);
    backend->DeleteShaderProgram(program);
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = nullptr; io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    backend = nullptr; context = nullptr; program = {}; vertices = {}; indices = {}; input = {};
    vertexCapacity = 0; indexCapacity = 0;
    vertexData = {}; indexData = {};
}

//准备当前上下文的渲染帧
void ImGuiRenderer::BeginFrame()
{
    if (context) ImGui::SetCurrentContext(context);
}

//上传或释放 ImGui 动态纹理
void ImGuiRenderer::UpdateTexture(ImTextureData& texture)
{
    if (texture.Status == ImTextureStatus_WantCreate)
    {
        if (texture.Format != ImTextureFormat_RGBA32) throw std::runtime_error("ImGui renderer requires RGBA32 textures.");
        GpuTextureDesc desc;
        desc.width = texture.Width; desc.height = texture.Height; desc.channels = 4;
        desc.pixels = static_cast<const uint8*>(texture.GetPixels()); desc.clampToEdge = true;
        GpuTextureID id = backend->CreateTexture(desc);
        if (!id.IsValid()) throw std::runtime_error("ImGui texture upload failed.");
        texture.SetTexID(static_cast<ImTextureID>(id.id));
        texture.BackendUserData = backend;
        texture.SetStatus(ImTextureStatus_OK);
    }
    else if (texture.Status == ImTextureStatus_WantUpdates)
    {
        for (const ImTextureRect& rect : texture.Updates)
            if (!backend->UploadTextureRegion({ static_cast<uint32>(texture.TexID) }, rect.x, rect.y, rect.w, rect.h,
                4, static_cast<const uint8*>(texture.GetPixelsAt(rect.x, rect.y)), texture.Width * 4)) throw std::runtime_error("ImGui texture update failed.");
        texture.SetStatus(ImTextureStatus_OK);
    }
    else if (texture.Status == ImTextureStatus_WantDestroy && texture.UnusedFrames > 0)
    {
        backend->DeleteTexture({ static_cast<uint32>(texture.TexID) });
        texture.SetTexID(ImTextureID_Invalid); texture.SetStatus(ImTextureStatus_Destroyed); texture.BackendUserData = nullptr;
    }
}

//提交绘制列表与像素裁剪
void ImGuiRenderer::Render(ImDrawData* data, bool beginPass)
{
    if (!backend || !data) return;
    ImGui::SetCurrentContext(context);
    if (data->Textures) for (ImTextureData* texture : *data->Textures) UpdateTexture(*texture);
    int32 width = static_cast<int32>(data->DisplaySize.x * data->FramebufferScale.x);
    int32 height = static_cast<int32>(data->DisplaySize.y * data->FramebufferScale.y);
    if (width <= 0 || height <= 0 || !data->TotalVtxCount || !data->TotalIdxCount) return;

    //合并绘制列表并展开顶点偏移
    vertexData.resize(static_cast<usize>(data->TotalVtxCount) * sizeof(ImDrawVert));
    indexData.resize(data->TotalIdxCount);
    uint32 vertexStart = 0;
    usize indexOffset = 0;
    for (const ImDrawList* list : data->CmdLists)
    {
        std::memcpy(vertexData.data() + static_cast<usize>(vertexStart) * sizeof(ImDrawVert),
            list->VtxBuffer.Data, static_cast<usize>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
        for (int32 index = 0; index < list->IdxBuffer.Size; ++index)
            indexData[indexOffset + index] = vertexStart + list->IdxBuffer[index];
        for (const ImDrawCmd& command : list->CmdBuffer)
            if (command.VtxOffset != 0)
                for (uint32 index = 0; index < command.ElemCount; ++index)
                    indexData[indexOffset + command.IdxOffset + index] += command.VtxOffset;
        vertexStart += list->VtxBuffer.Size;
        indexOffset += list->IdxBuffer.Size;
    }
    usize vertexBytes = vertexData.size();
    uint32 indexCount = static_cast<uint32>(indexData.size());
    if (!vertices.IsValid())
    {
        vertexCapacity = std::max<usize>(vertexBytes, 65536);
        indexCapacity = std::max<uint32>(indexCount, 16384);
        vertices = backend->CreateVertexBuffer({ nullptr, vertexCapacity, GpuBufferUsage::Stream });
        indices = backend->CreateIndexBuffer({ nullptr, static_cast<usize>(indexCapacity) * sizeof(uint32), GpuBufferUsage::Stream });
        input = backend->CreateVertexInput({ vertices, indices, sizeof(ImDrawVert), GpuVertexLayout::ImGui });
    }
    vertexCapacity = std::max(vertexCapacity, vertexBytes); indexCapacity = std::max(indexCapacity, indexCount);
    if (!backend->UploadVertexBuffer(vertices, vertexData.data(), vertexBytes, vertexCapacity) || !backend->UploadIndexBuffer(indices, indexData.data(), indexCount, indexCapacity)) throw std::runtime_error("ImGui mesh upload failed.");

    //构造屏幕投影与混合状态
    matrix4x4 projection = {};
    projection.m[0] = 2.0f / data->DisplaySize.x;
    projection.m[5] = -2.0f / data->DisplaySize.y;
    projection.m[10] = -1.0f;
    projection.m[12] = -1.0f - data->DisplayPos.x * projection.m[0];
    projection.m[13] = 1.0f - data->DisplayPos.y * projection.m[5];
    projection.m[15] = 1.0f;
    PipelineState saved = backend->GetPipelineState();
    if (beginPass)
    {
        RenderPassDesc pass; pass.width = width; pass.height = height; pass.clearMode = ClearMode::None;
        backend->BeginPass(pass);
    }
    //绑定共用绘制状态
    auto bindRenderState = [&]()
    {
        PipelineState& state = backend->GetPipelineState();
        state = {}; state.depthTest = state.depthWrite = false; state.blend = true;
        backend->BindShaderProgram(program); backend->BindVertexInput(input);
        ShaderParameters& parameters = backend->GetParameters();
        parameters.SetMatrix4("u_Projection", projection);
        parameters.SetInt("u_Texture", 0);
    };
    bindRenderState();
    uint32 indexStart = 0;
    for (const ImDrawList* list : data->CmdLists)
    {
        for (const ImDrawCmd& command : list->CmdBuffer)
        {
            if (command.UserCallback)
            {
                if (command.UserCallback != ImDrawCallback_ResetRenderState) command.UserCallback(list, &command);
                bindRenderState();
                continue;
            }
            float32 left = (command.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x;
            float32 top = (command.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y;
            float32 right = (command.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x;
            float32 bottom = (command.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y;
            int32 x0 = std::clamp(static_cast<int32>(std::floor(left)), 0, width), y0 = std::clamp(static_cast<int32>(std::floor(top)), 0, height);
            int32 x1 = std::clamp(static_cast<int32>(std::ceil(right)), 0, width), y1 = std::clamp(static_cast<int32>(std::ceil(bottom)), 0, height);
            if (x1 <= x0 || y1 <= y0) continue;
            backend->SetScissor(true, x0, height - y1, x1 - x0, y1 - y0);
            backend->BindTexture(0, { static_cast<uint32>(command.GetTexID()) });
            backend->DrawIndexed(indexStart + command.IdxOffset, command.ElemCount);
        }
        indexStart += list->IdxBuffer.Size;
    }
    backend->SetScissor(false);
    backend->GetPipelineState() = saved;
    backend->BindVertexInput({}); backend->BindShaderProgram({});
    if (beginPass) backend->EndPass();
}
