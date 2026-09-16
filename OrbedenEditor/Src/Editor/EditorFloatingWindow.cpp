#include "Editor/EditorFloatingWindow.h"

#include "Editor/EditorGUI.h"
#include "Log/Log.h"

#include <glad/gl.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>

EditorFloatingWindow::~EditorFloatingWindow()
{
    Destroy();
}

//创建共享主窗口 OpenGL 上下文的独立面板窗口
bool EditorFloatingWindow::Create(const std::string& title, const vector2& position, const vector2& size)
{
    Destroy();

    mainWindow = EditorGUI::GetMainGlfwWindow();
    mainContext = EditorGUI::GetMainContext();
    ImFontAtlas* sharedFonts = EditorGUI::GetFontAtlas();
    if (!mainWindow || !mainContext || !sharedFonts) return false;

    //与主窗口一致的 GLFW 提示，并以主窗口为共享上下文
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(std::max(static_cast<int32>(size.x), 160), std::max(static_cast<int32>(size.y), 120),
        title.c_str(), nullptr, mainWindow);
    if (!window)
    {
        Log::Error("Editor floating window create failed.");
        mainWindow = nullptr;
        mainContext = nullptr;
        return false;
    }

    glfwSetWindowPos(window, static_cast<int32>(position.x), static_cast<int32>(position.y));
    glfwMakeContextCurrent(window);

    //独立窗口使用专属 ImGui 上下文并共享主上下文的字体图集
    context = ImGui::CreateContext(sharedFonts);
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true) || !ImGui_ImplOpenGL3_Init("#version 430"))
    {
        Log::Error("Editor floating window ImGui backend initialize failed.");
        Destroy();
        return false;
    }

    EditorGUI::RegisterDragWindow(window);
    glfwGetWindowPos(window, &lastWindowX, &lastWindowY);

    //创建发生在主上下文帧内，必须同时恢复 GL 与 ImGui 上下文
    glfwMakeContextCurrent(mainWindow);
    if (mainContext) ImGui::SetCurrentContext(mainContext);
    return true;
}

//销毁独立窗口与专属 ImGui 上下文
void EditorFloatingWindow::Destroy()
{
    if (!window)
    {
        mainWindow = nullptr;
        mainContext = nullptr;
        return;
    }

    EditorGUI::UnregisterDragWindow(window);

    //两个后端都必须在自己的上下文为当前时关闭，字体图集由主上下文持有
    glfwMakeContextCurrent(window);
    ImGui::SetCurrentContext(context);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(context);
    context = nullptr;

    //独立窗口不参与主窗口的 glfwTerminate 引用计数，此处绝不调用 glfwTerminate
    glfwDestroyWindow(window);
    window = nullptr;
    if (mainWindow) glfwMakeContextCurrent(mainWindow);
    if (mainContext) ImGui::SetCurrentContext(mainContext);

    mainWindow = nullptr;
    mainContext = nullptr;
    titleBarDragging = false;
    dockBackRequested = false;
    itemActive = false;
    lastWindowX = 0;
    lastWindowY = 0;
}

//开始本窗口的 ImGui 帧，窗口不可见时返回 false
bool EditorFloatingWindow::BeginFrame()
{
    if (!window || !context) return false;
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
    {
        itemActive = false;
        return false;
    }

    //标题栏拖动期间标记待定的回主窗口停靠，松开左键时提交
    int32 windowX = 0;
    int32 windowY = 0;
    glfwGetWindowPos(window, &windowX, &windowY);
    bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (leftDown && (windowX != lastWindowX || windowY != lastWindowY)) titleBarDragging = true;
    if (!leftDown)
    {
        if (titleBarDragging) dockBackRequested = IsCursorInsideMainWindow();
        titleBarDragging = false;
    }
    lastWindowX = windowX;
    lastWindowY = windowY;

    glfwMakeContextCurrent(window);
    ImGui::SetCurrentContext(context);
    EditorGUI::ApplyTheme();
    ImGui_ImplGlfw_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    return true;
}

//提交本窗口的 ImGui 绘制并交换缓冲
void EditorFloatingWindow::EndFrame()
{
    if (!window || !context) return;

    itemActive = ImGui::IsAnyItemActive();
    ImGui::Render();

    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);

    //后续渲染与绘制始终以主窗口为准
    if (mainWindow) glfwMakeContextCurrent(mainWindow);
    if (mainContext) ImGui::SetCurrentContext(mainContext);
}

//判断系统关闭按钮是否被触发
bool EditorFloatingWindow::ShouldClose() const
{
    return window && glfwWindowShouldClose(window) != 0;
}

//获取并清除标题栏拖动结束后的回主窗口停靠请求
bool EditorFloatingWindow::TakeDockBackRequest()
{
    bool requested = dockBackRequested;
    dockBackRequested = false;
    return requested;
}

//判断本窗口是否有控件处于活动状态
bool EditorFloatingWindow::IsItemActive() const
{
    return itemActive;
}

//获取窗口在屏幕坐标中的位置
vector2 EditorFloatingWindow::GetPosition() const
{
    if (!window) return { 0.0f, 0.0f };

    int32 x = 0;
    int32 y = 0;
    glfwGetWindowPos(window, &x, &y);
    return { static_cast<float32>(x), static_cast<float32>(y) };
}

//获取窗口逻辑尺寸
vector2 EditorFloatingWindow::GetSize() const
{
    if (!window) return { 0.0f, 0.0f };

    int32 width = 0;
    int32 height = 0;
    glfwGetWindowSize(window, &width, &height);
    return { static_cast<float32>(width), static_cast<float32>(height) };
}

//判断鼠标是否位于主窗口客户区内
bool EditorFloatingWindow::IsCursorInsideMainWindow() const
{
    if (!window || !mainWindow) return false;

    double cursorX = 0.0;
    double cursorY = 0.0;
    int32 ownX = 0;
    int32 ownY = 0;
    int32 mainX = 0;
    int32 mainY = 0;
    int32 mainWidth = 0;
    int32 mainHeight = 0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    glfwGetWindowPos(window, &ownX, &ownY);
    glfwGetWindowPos(mainWindow, &mainX, &mainY);
    glfwGetWindowSize(mainWindow, &mainWidth, &mainHeight);

    double screenX = static_cast<double>(ownX) + cursorX;
    double screenY = static_cast<double>(ownY) + cursorY;
    return screenX >= static_cast<double>(mainX) && screenX < static_cast<double>(mainX + mainWidth)
        && screenY >= static_cast<double>(mainY) && screenY < static_cast<double>(mainY + mainHeight);
}
