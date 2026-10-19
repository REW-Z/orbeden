#pragma once

#include "Defines/types.h"
#include "Rendering/ImGuiRenderer.h"

class IWindow;
struct ImGuiContext;

//ImGui 调试 UI 层，负责 GLFW 与公共渲染 后端接入和调试 overlay 绘制
class ImGuiLayer
{
private:
    bool initialized = false;
    ImGuiContext* context = nullptr;
    ImGuiRenderer renderer;

public:
    //初始化 ImGui GLFW 与公共渲染 后端
    bool Initialize(IWindow* window, RenderBackend& backend);

    //关闭 ImGui 后端并销毁上下文
    void Shutdown();

    //开始一帧 ImGui 绘制
    void BeginFrame();

    //绘制左上角 FPS 标签
    void DrawFpsLabel();

    //提交 ImGui 绘制数据
    void Render();

    //判断 ImGui 层是否已经可用
    bool IsInitialized() const;
};
