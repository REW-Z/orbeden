#pragma once

#include "Defines/types.h"
#include "Runtime/EngineTypes.h"
#include "Runtime/Native/NativeApiAbi.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <array>
#include <string>

class IWindow;

#pragma pack(push, 8)

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
    void* beginChild = nullptr;
    void* endChild = nullptr;
    void* treeNode = nullptr;
    void* treePop = nullptr;
    void* openPopup = nullptr;
    void* beginPopup = nullptr;
    void* closePopup = nullptr;
    void* dragSource = nullptr;
    void* readDrag = nullptr;
    void* acceptDrag = nullptr;
    void* fillRemainingArea = nullptr;
    void* getDropPlacement = nullptr;
    void* setTheme = nullptr;
    void* beginPanelContent = nullptr;
    void* endPanelContent = nullptr;
    void* drawSceneView = nullptr;
    void* resolveSceneDropPosition = nullptr;
    void* referenceField = nullptr;
    void* assetTile = nullptr;
    void* viewToggleButton = nullptr;
    void* textColored = nullptr;
    void* textWrapped = nullptr;
    void* setScrollHereY = nullptr;
    void* getContentRegionAvail = nullptr;
    void* getCursorScreenPos = nullptr;
    void* drawRects = nullptr;
    void* drawTextClipped = nullptr;
    void* invisibleButton = nullptr;
    void* isItemHovered = nullptr;
    void* isItemClicked = nullptr;
    void* getMousePos = nullptr;
    void* setTooltip = nullptr;
    void* inputTextMultiline = nullptr;
    void* getMouseWheel = nullptr;
    void* isWindowFocused = nullptr;
    void* toggleButton = nullptr;
    void* renameInput = nullptr;
    void* beginDialog = nullptr;
    void* assetRenameTile = nullptr;
    void* sliderFloat = nullptr;
    void* calcButtonWidth = nullptr;
    void* beginMenu = nullptr;
    void* endMenu = nullptr;
};

#pragma pack(pop)

ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorGuiNativeApi, 73);

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
    //应用共享主题到当前 ImGui 上下文
    static void ApplyTheme();

    //获取主题定义的停靠分隔条尺寸
    static float32 GetSplitterSize();

    //获取主题定义的内容边距
    static ImVec2 GetWindowPadding();

    //获取主题定义的强调色
    static ImU32 GetActiveColor();

    //获取主题定义的边框色
    static ImU32 GetBorderColor();

    //获取主题定义的面板圆角半径
    static float32 GetPanelCornerRadius();

    //记录跨 Panel 共享拖拽载荷
    static void SetDragPayload(int32 kind, const std::string& key);

    //注册参与跨窗口拖拽判断的编辑器窗口
    static void RegisterDragWindow(GLFWwindow* window);

    //注销参与跨窗口拖拽判断的编辑器窗口
    static void UnregisterDragWindow(GLFWwindow* window);

    //判断任一编辑器窗口中左键是否按下
    static bool IsLeftMouseDownAnywhere();

    //判断鼠标是否已移出主窗口客户区
    static bool IsCursorOutsideMainWindow();

    //获取鼠标在主窗口客户区中的屏幕坐标
    static vector2 GetCursorScreenPosition();

    //获取主窗口 GLFW 句柄
    static GLFWwindow* GetMainGlfwWindow();

    //获取主 ImGui 上下文
    static ImGuiContext* GetMainContext();

    //获取主上下文共享的字体图集
    static ImFontAtlas* GetFontAtlas();

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

    //按主题背景色清空主窗口帧缓冲
    void ClearMainFramebuffer();

    //读取场景相机滚轮增量
    float32 ConsumeSceneMouseWheel();

    //判断 EditorGUI 是否可用
    bool IsInitialized() const;
};
