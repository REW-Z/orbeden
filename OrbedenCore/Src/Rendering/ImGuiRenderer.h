#pragma once

#include "Rendering/Backend/RenderBackend.h"

struct ImDrawData;
struct ImGuiContext;
struct ImTextureData;

//通过公共渲染接口提交 ImGui
class ImGuiRenderer
{
private:
    RenderBackend* backend = nullptr;
    ImGuiContext* context = nullptr;
    GpuShaderProgramID program;
    GpuVertexBufferID vertices;
    GpuIndexBufferID indices;
    GpuVertexInputID input;
    usize vertexCapacity = 0;
    uint32 indexCapacity = 0;
    List<uint8> vertexData;
    List<uint32> indexData;

    //上传或释放 ImGui 动态纹理
    void UpdateTexture(ImTextureData& texture);

public:
    //绑定当前 ImGui 上下文与渲染后端
    bool Initialize(RenderBackend& value);
    //释放上下文持有的渲染资源
    void Shutdown();
    //准备当前上下文的渲染帧
    void BeginFrame();
    //提交绘制列表与像素裁剪
    void Render(ImDrawData* data, bool beginPass = true);
};
