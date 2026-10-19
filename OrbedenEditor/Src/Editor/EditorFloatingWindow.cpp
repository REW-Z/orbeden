#include "Editor/EditorFloatingWindow.h"

#include "Editor/EditorGUI.h"
#include "Log/Log.h"
#include "Profiler/Profiler.h"

#include "Application.h"
#include "Rendering/RenderSystem.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>

EditorFloatingWindow::~EditorFloatingWindow()
{
    Destroy();
}

//创建共享主窗口设备资源的独立面板窗口
bool EditorFloatingWindow::Create(const std::string& title, const vector2& position, const vector2& size)
{
    Destroy();

    mainWindow = EditorGUI::GetMainGlfwWindow();
    mainContext = EditorGUI::GetMainContext();
    ImFontAtlas* sharedFonts = EditorGUI::GetFontAtlas();
    if (!mainWindow || !mainContext || !sharedFonts) return false;

    backend = &RenderSystem::Current()->GetBackend();
    IWindow* main = Application::Current()->GetWindow();
    WindowDesc desc;
    desc.title = title;
    desc.width = std::max(static_cast<int32>(size.x), 160);
    desc.height = std::max(static_cast<int32>(size.y), 120);
    desc.graphicsApi = main->GetGraphicsApi();
    desc.sharedResourcesWindow = main;
    desc.receivesEngineInput = false;
    platformWindow = std::make_unique<GlfwWindow>();
    if (platformWindow->Create(desc)) window = platformWindow->GetGlfwWindow();
    if (!window)
    {
        Log::Error("Editor floating window create failed.");
        mainWindow = nullptr;
        mainContext = nullptr;
        return false;
    }

    glfwSetWindowPos(window, static_cast<int32>(position.x), static_cast<int32>(position.y));

    //独立窗口使用专属 ImGui 上下文并共享主上下文的字体图集
    context = ImGui::CreateContext(sharedFonts);
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    platformInitialized = ImGui_ImplGlfw_InitForOther(window, true);
    if (!platformInitialized || !renderer.Initialize(*backend))
    {
        Log::Error("Editor floating window ImGui backend initialize failed.");
        Destroy();
        return false;
    }

    surface = backend->CreateSurface(platformWindow.get());
    if (!surface.IsValid()) { Destroy(); return false; }
    EditorGUI::RegisterDragWindow(window);
    glfwGetWindowPos(window, &lastWindowX, &lastWindowY);

    //恢复主窗口与 ImGui 上下文
    backend->SetSurface({});
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

    //释放窗口的绘制与输入资源
    ImGui::SetCurrentContext(context);
    renderer.Shutdown();
    if (platformInitialized) ImGui_ImplGlfw_Shutdown();
    platformInitialized = false;
    ImGui::DestroyContext(context);
    context = nullptr;

    //释放呈现表面与平台窗口
    if (backend) backend->DeleteSurface(surface);
    surface = {};
    platformWindow.reset();
    window = nullptr;
    if (backend) backend->SetSurface({});
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

    ImGui::SetCurrentContext(context);
    EditorGUI::ApplyTheme();
    ImGui_ImplGlfw_NewFrame();
    renderer.BeginFrame();
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
    backend->SetSurface(surface);
    {
        PROFILE("Render/FloatingWindow/DrawGui");
        renderer.Render(ImGui::GetDrawData());
    }

    //后续渲染与绘制始终以主窗口为准
    if (backend) backend->SetSurface({});
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
