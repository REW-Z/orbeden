#pragma once

#include "Defines/types.h"
#include "Runtime/EngineTypes.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <array>

class IWindow;

struct EditorGuiNativeApi
{
public:
    void* label = nullptr;
    void* button = nullptr;
    void* beginComponentBlock = nullptr;
    void* endComponentBlock = nullptr;
    void* beginCollapsibleComponentBlock = nullptr;
    void* beginCombo = nullptr;
    void* endCombo = nullptr;
    void* selectable = nullptr;
    void* checkbox = nullptr;
    void* inputInt = nullptr;
    void* inputFloat = nullptr;
    void* inputVector3 = nullptr;
    void* inputText = nullptr;
    void* separator = nullptr;
    void* sameLine = nullptr;
    void* beginTable = nullptr;
    void* endTable = nullptr;
    void* tableSetupColumn = nullptr;
    void* tableHeadersRow = nullptr;
    void* tableNextRow = nullptr;
    void* tableSetColumnIndex = nullptr;
    void* tableSelectable = nullptr;
    void* isItemDoubleClicked = nullptr;
    void* beginPopupContextItem = nullptr;
    void* beginPopupContextWindow = nullptr;
    void* endPopup = nullptr;
    void* menuItem = nullptr;
    void* setClipboardText = nullptr;
    void* beginDisabled = nullptr;
    void* endDisabled = nullptr;
};

//Editor ImGui 绑定层
class EditorGUI
{
private:
    static EditorGUI* activeInstance;

    IWindow* window = nullptr;
    GLFWwindow* glfwWindow = nullptr;
    ImGuiContext* context = nullptr;
    std::array<GLFWcursor*, ImGuiMouseCursor_COUNT> mouseCursors{};
    GLFWwindowfocusfun previousWindowFocusCallback = nullptr;
    GLFWcursorenterfun previousCursorEnterCallback = nullptr;
    GLFWcursorposfun previousCursorPositionCallback = nullptr;
    GLFWmousebuttonfun previousMouseButtonCallback = nullptr;
    GLFWscrollfun previousScrollCallback = nullptr;
    GLFWkeyfun previousKeyCallback = nullptr;
    GLFWcharfun previousCharCallback = nullptr;
    double previousTime = 0.0;
    float32 sceneMouseWheel = 0.0f;
    bool initialized = false;

    //转换 GLFW 按键
    static ImGuiKey ConvertKey(int32 key);

    //更新按键修饰状态
    static void UpdateKeyModifiers(ImGuiIO& io, int32 modifiers);

    //接收窗口焦点事件
    static void WindowFocusCallback(GLFWwindow* window, int32 focused);

    //接收鼠标进入事件
    static void CursorEnterCallback(GLFWwindow* window, int32 entered);

    //接收鼠标位置事件
    static void CursorPositionCallback(GLFWwindow* window, double x, double y);

    //接收鼠标按键事件
    static void MouseButtonCallback(GLFWwindow* window, int32 button, int32 action, int32 modifiers);

    //接收鼠标滚轮事件
    static void ScrollCallback(GLFWwindow* window, double x, double y);

    //接收键盘事件
    static void KeyCallback(GLFWwindow* window, int32 key, int32 scanCode, int32 action, int32 modifiers);

    //接收文本输入事件
    static void CharacterCallback(GLFWwindow* window, uint32 codePoint);

    //更新鼠标光标
    void UpdateMouseCursor();

public:
    //初始化 Editor ImGui
    bool Initialize(IWindow* editorWindow);

    //关闭 Editor ImGui
    void Shutdown();

    //开始 EditorGUI 帧
    void BeginFrame();

    //提交 EditorGUI 绘制
    void Render();

    //获取托管 EditorGUI API
    EditorGuiNativeApi GetNativeApi() const;

    //读取场景相机滚轮增量
    float32 ConsumeSceneMouseWheel();

    //判断 EditorGUI 是否可用
    bool IsInitialized() const;
};
