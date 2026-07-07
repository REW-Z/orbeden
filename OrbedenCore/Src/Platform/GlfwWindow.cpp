#include "Platform/GlfwWindow.h"

#include <array>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_NATIVE_INCLUDE_NONE
struct HWND__;
typedef HWND__* HWND;
#include <GLFW/glfw3native.h>
#endif

#include "Log/Log.h"
#include "Platform/InputManager.h"

namespace
{
    bool glfwInitialized = false;
    uint32 glfwWindowCount = 0;
    std::array<KeyEnum, GLFW_KEY_LAST + 1> keyMap{};
    std::array<KeyEnum, GLFW_MOUSE_BUTTON_LAST + 1> mouseMap{};

    void InitializeKeyMap()
    {
        keyMap.fill(KeyEnum::UNMAPPED);
        mouseMap.fill(KeyEnum::UNMAPPED);

        mouseMap[GLFW_MOUSE_BUTTON_LEFT] = KeyEnum::MOUSEL;
        mouseMap[GLFW_MOUSE_BUTTON_RIGHT] = KeyEnum::MOUSER;
        mouseMap[GLFW_MOUSE_BUTTON_MIDDLE] = KeyEnum::MOUSEMID;

        keyMap[GLFW_KEY_1] = KeyEnum::NUM1;
        keyMap[GLFW_KEY_2] = KeyEnum::NUM2;
        keyMap[GLFW_KEY_3] = KeyEnum::NUM3;
        keyMap[GLFW_KEY_4] = KeyEnum::NUM4;
        keyMap[GLFW_KEY_5] = KeyEnum::NUM5;
        keyMap[GLFW_KEY_6] = KeyEnum::NUM6;
        keyMap[GLFW_KEY_7] = KeyEnum::NUM7;
        keyMap[GLFW_KEY_8] = KeyEnum::NUM8;
        keyMap[GLFW_KEY_9] = KeyEnum::NUM9;
        keyMap[GLFW_KEY_0] = KeyEnum::NUM0;
        keyMap[GLFW_KEY_Q] = KeyEnum::Q;
        keyMap[GLFW_KEY_W] = KeyEnum::W;
        keyMap[GLFW_KEY_E] = KeyEnum::E;
        keyMap[GLFW_KEY_R] = KeyEnum::R;
        keyMap[GLFW_KEY_T] = KeyEnum::T;
        keyMap[GLFW_KEY_Y] = KeyEnum::Y;
        keyMap[GLFW_KEY_U] = KeyEnum::U;
        keyMap[GLFW_KEY_I] = KeyEnum::I;
        keyMap[GLFW_KEY_O] = KeyEnum::O;
        keyMap[GLFW_KEY_P] = KeyEnum::P;
        keyMap[GLFW_KEY_A] = KeyEnum::A;
        keyMap[GLFW_KEY_S] = KeyEnum::S;
        keyMap[GLFW_KEY_D] = KeyEnum::D;
        keyMap[GLFW_KEY_F] = KeyEnum::F;
        keyMap[GLFW_KEY_G] = KeyEnum::G;
        keyMap[GLFW_KEY_H] = KeyEnum::H;
        keyMap[GLFW_KEY_J] = KeyEnum::J;
        keyMap[GLFW_KEY_K] = KeyEnum::K;
        keyMap[GLFW_KEY_L] = KeyEnum::L;
        keyMap[GLFW_KEY_Z] = KeyEnum::Z;
        keyMap[GLFW_KEY_X] = KeyEnum::X;
        keyMap[GLFW_KEY_C] = KeyEnum::C;
        keyMap[GLFW_KEY_V] = KeyEnum::V;
        keyMap[GLFW_KEY_B] = KeyEnum::B;
        keyMap[GLFW_KEY_N] = KeyEnum::N;
        keyMap[GLFW_KEY_M] = KeyEnum::M;
        keyMap[GLFW_KEY_SPACE] = KeyEnum::SPACE;
        keyMap[GLFW_KEY_TAB] = KeyEnum::TAB;
        keyMap[GLFW_KEY_LEFT_SHIFT] = KeyEnum::LSHIFT;
        keyMap[GLFW_KEY_LEFT_CONTROL] = KeyEnum::LCTRL;
        keyMap[GLFW_KEY_LEFT_ALT] = KeyEnum::LALT;
        keyMap[GLFW_KEY_BACKSPACE] = KeyEnum::BACKSPACE;
        keyMap[GLFW_KEY_ENTER] = KeyEnum::ENTER;
        keyMap[GLFW_KEY_RIGHT_SHIFT] = KeyEnum::RSHIFT;
        keyMap[GLFW_KEY_RIGHT_CONTROL] = KeyEnum::RCTRL;
        keyMap[GLFW_KEY_RIGHT_ALT] = KeyEnum::RALT;
        keyMap[GLFW_KEY_UP] = KeyEnum::UP;
        keyMap[GLFW_KEY_DOWN] = KeyEnum::DOWN;
        keyMap[GLFW_KEY_LEFT] = KeyEnum::LEFT;
        keyMap[GLFW_KEY_RIGHT] = KeyEnum::RIGHT;
    }

    bool StartGlfw()
    {
        if (glfwInitialized) return true;

        if (!glfwInit())
        {
            Log::Error("GLFW initialize failed.");
            return false;
        }

        InitializeKeyMap();
        glfwInitialized = true;
        return true;
    }

    KeyEnum MapKey(int key)
    {
        if (key < 0 || key > GLFW_KEY_LAST) return KeyEnum::UNMAPPED;
        return keyMap[static_cast<usize>(key)];
    }

    KeyEnum MapMouseButton(int button)
    {
        if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return KeyEnum::UNMAPPED;
        return mouseMap[static_cast<usize>(button)];
    }

    void KeyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods)
    {
        (void)glfwWindow;
        (void)scancode;
        (void)mods;

        if (action == GLFW_PRESS)
        {
            InputManager::SetKeyState(MapKey(key), true);
        }
        else if (action == GLFW_RELEASE)
        {
            InputManager::SetKeyState(MapKey(key), false);
        }
    }

    void MouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods)
    {
        (void)glfwWindow;
        (void)mods;

        if (action == GLFW_PRESS)
        {
            InputManager::SetKeyState(MapMouseButton(button), true);
        }
        else if (action == GLFW_RELEASE)
        {
            InputManager::SetKeyState(MapMouseButton(button), false);
        }
    }

    void CursorPosCallback(GLFWwindow* glfwWindow, double x, double y)
    {
        (void)glfwWindow;
        InputManager::SetMousePosition(static_cast<float32>(x), static_cast<float32>(y));
    }

    void FramebufferSizeCallback(GLFWwindow* glfwWindow, int newWidth, int newHeight)
    {
        GlfwWindow* owner = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(glfwWindow));
        if (owner)
        {
            owner->HandleFramebufferResize(newWidth, newHeight);
        }
    }
}

//确保窗口销毁时释放 GLFW 资源
GlfwWindow::~GlfwWindow()
{
    Destroy();
}

//创建 GLFW 窗口
bool GlfwWindow::Create(const WindowDesc& newDesc)
{
    Destroy();

    if (!StartGlfw()) return false;

    desc = newDesc;
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);

    if (desc.graphicsApi == WindowGraphicsApi::None || desc.graphicsApi == WindowGraphicsApi::Vulkan)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    else if (desc.graphicsApi == WindowGraphicsApi::OpenGL)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
    if (!window)
    {
        Log::Error("GLFW window create failed.");
        if (glfwInitialized && glfwWindowCount == 0)
        {
            glfwTerminate();
            glfwInitialized = false;
        }
        return false;
    }

    glfwWindowCount++;
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);

    if (desc.graphicsApi == WindowGraphicsApi::OpenGL)
    {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(desc.vsync ? 1 : 0);
    }

    RefreshSize();
    HandleFramebufferResize(framebufferWidth, framebufferHeight);
    return true;
}

//销毁 GLFW 窗口
void GlfwWindow::Destroy()
{
    if (!window) return;

    glfwDestroyWindow(window);
    window = nullptr;

    if (glfwWindowCount > 0)
    {
        glfwWindowCount--;
    }

    if (glfwInitialized && glfwWindowCount == 0)
    {
        glfwTerminate();
        glfwInitialized = false;
    }
}

//处理 GLFW 事件
void GlfwWindow::PollEvents()
{
    if (!window) return;

    glfwPollEvents();
}

//提交当前帧显示
void GlfwWindow::Present()
{
    if (!window) return;

    if (desc.graphicsApi == WindowGraphicsApi::OpenGL)
    {
        glfwSwapBuffers(window);
    }
}

//判断窗口是否请求关闭
bool GlfwWindow::ShouldClose() const
{
    return !window || glfwWindowShouldClose(window);
}

//设置 resize 监听者
void GlfwWindow::SetResizeListener(IWindowResizeListener* listener)
{
    resizeListener = listener;
}

//获取窗口逻辑宽度
int32 GlfwWindow::GetWidth() const
{
    return width;
}

//获取窗口逻辑高度
int32 GlfwWindow::GetHeight() const
{
    return height;
}

//获取 framebuffer 宽度
int32 GlfwWindow::GetFramebufferWidth() const
{
    return framebufferWidth;
}

//获取 framebuffer 高度
int32 GlfwWindow::GetFramebufferHeight() const
{
    return framebufferHeight;
}

//获取窗口图形 API
WindowGraphicsApi GlfwWindow::GetGraphicsApi() const
{
    return desc.graphicsApi;
}

//获取平台原生窗口句柄
void* GlfwWindow::GetNativeHandle() const
{
#ifdef _WIN32
    return window ? static_cast<void*>(glfwGetWin32Window(window)) : nullptr;
#else
    return nullptr;
#endif
}

//获取 GLFW 窗口指针，供后续渲染后端对接
GLFWwindow* GlfwWindow::GetGlfwWindow() const
{
    return window;
}

//同步 GLFW 当前窗口尺寸
void GlfwWindow::RefreshSize()
{
    if (!window)
    {
        width = 0;
        height = 0;
        framebufferWidth = 0;
        framebufferHeight = 0;
        return;
    }

    int newWidth = 0;
    int newHeight = 0;
    glfwGetWindowSize(window, &newWidth, &newHeight);
    width = newWidth;
    height = newHeight;

    int newFramebufferWidth = 0;
    int newFramebufferHeight = 0;
    glfwGetFramebufferSize(window, &newFramebufferWidth, &newFramebufferHeight);
    framebufferWidth = newFramebufferWidth;
    framebufferHeight = newFramebufferHeight;
}

//派发 framebuffer resize 事件
void GlfwWindow::HandleFramebufferResize(int32 newWidth, int32 newHeight)
{
    framebufferWidth = newWidth;
    framebufferHeight = newHeight;

    if (resizeListener)
    {
        resizeListener->OnWindowResize(framebufferWidth, framebufferHeight);
    }
}
