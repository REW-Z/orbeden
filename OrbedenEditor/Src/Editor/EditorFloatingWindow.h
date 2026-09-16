#pragma once

#include "Runtime/EngineTypes.h"

#include <string>

struct GLFWwindow;
struct ImGuiContext;

//承载单个编辑器面板的独立 GLFW 窗口与专属 ImGui 上下文。
class EditorFloatingWindow
{
private:
    GLFWwindow* mainWindow = nullptr;
    GLFWwindow* window = nullptr;
    ImGuiContext* mainContext = nullptr;
    ImGuiContext* context = nullptr;
    bool titleBarDragging = false;
    bool dockBackRequested = false;
    bool itemActive = false;
    int32 lastWindowX = 0;
    int32 lastWindowY = 0;

    //判断鼠标是否位于主窗口客户区内
    bool IsCursorInsideMainWindow() const;

public:
    EditorFloatingWindow() = default;
    EditorFloatingWindow(const EditorFloatingWindow&) = delete;
    EditorFloatingWindow& operator=(const EditorFloatingWindow&) = delete;

    /// <summary>销毁独立窗口与专属 ImGui 上下文。</summary>
    ~EditorFloatingWindow();

    /// <summary>创建共享主窗口 OpenGL 上下文的独立面板窗口。</summary>
    bool Create(const std::string& title, const vector2& position, const vector2& size);

    /// <summary>销毁独立窗口与专属 ImGui 上下文。</summary>
    void Destroy();

    /// <summary>开始本窗口的 ImGui 帧，窗口不可见时返回 false。</summary>
    bool BeginFrame();

    /// <summary>提交本窗口的 ImGui 绘制并交换缓冲。</summary>
    void EndFrame();

    /// <summary>判断系统关闭按钮是否被触发。</summary>
    bool ShouldClose() const;

    /// <summary>获取并清除标题栏拖动结束后的回主窗口停靠请求。</summary>
    bool TakeDockBackRequest();

    /// <summary>判断本窗口是否有控件处于活动状态。</summary>
    bool IsItemActive() const;

    /// <summary>获取窗口在屏幕坐标中的位置。</summary>
    vector2 GetPosition() const;

    /// <summary>获取窗口逻辑尺寸。</summary>
    vector2 GetSize() const;
};
