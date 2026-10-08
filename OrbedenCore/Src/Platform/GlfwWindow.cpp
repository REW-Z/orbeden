#include "Platform/GlfwWindow.h"

#include <array>
#include <cstdio>
#include <glad/gl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_NATIVE_INCLUDE_NONE
struct HWND__;
typedef HWND__* HWND;
#include <GLFW/glfw3native.h>

#ifdef _WIN32
//本 TU 按上面的做法不引 windows.h，这两个 winmm 导出按同样方式直接声明
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int period);
extern "C" __declspec(dllimport) unsigned int __stdcall timeEndPeriod(unsigned int period);
#endif
#endif

#include "Log/Log.h"
#include "Profiler/Profiler.h"
#include "InputManager/InputEvent.h"
#include "InputManager/InputManager.h"

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
        keyMap[GLFW_KEY_HOME] = KeyEnum::HOME;
        keyMap[GLFW_KEY_END] = KeyEnum::END;
        keyMap[GLFW_KEY_DELETE] = KeyEnum::DEL;
        keyMap[GLFW_KEY_ESCAPE] = KeyEnum::ESCAPE;
    }

    bool StartGlfw()
    {
        if (glfwInitialized) return true;

        if (!glfwInit())
        {
            Log::Error("GLFW initialize failed.");
            return false;
        }

#ifdef _WIN32
        //把系统计时器精度提到 1ms。默认粒度是 15.6ms，sleep_for(1ms) 会睡满一个 tick，
        //帧节流器因此会偶尔睡过目标时刻，表现为偶发的整帧晚一拍。
        timeBeginPeriod(1);
#endif

        InitializeKeyMap();
        glfwInitialized = true;
        return true;
    }

    //归还系统计时器精度
    void StopGlfwTimer()
    {
#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }

    //事件种类与设备，与 UIInputKind/UIInputDevice 的数值一致。
    constexpr uint32 KindPointerMove = 0;
    constexpr uint32 KindPointerDown = 1;
    constexpr uint32 KindPointerUp = 2;
    constexpr uint32 KindWheel = 4;
    constexpr uint32 KindKeyDown = 5;
    constexpr uint32 KindKeyUp = 6;
    constexpr uint32 KindTextCommit = 7;
    constexpr uint32 KindWindowFocusLost = 12;

    constexpr uint32 DeviceMouse = 0;
    constexpr uint32 DeviceKeyboard = 3;

    //鼠标按键的 KeyEnum 值同时作为指针按钮标识：Left=0、Right=1、Middle=2。
    uint32 PointerButtonOf(KeyEnum key)
    {
        if (key == KeyEnum::MOUSER) return 1;
        if (key == KeyEnum::MOUSEMID) return 2;
        return 0;
    }

    uint32 ModifierBits(int mods)
    {
        uint32 bits = 0;
        if (mods & GLFW_MOD_SHIFT) bits |= 1;
        if (mods & GLFW_MOD_CONTROL) bits |= 2;
        if (mods & GLFW_MOD_ALT) bits |= 4;
        return bits;
    }

    //原始键与鼠标位置由回调写入；事件带的是那一时刻的位置。
    vector2 CurrentPointerPosition()
    {
        return InputManager::MousePos();
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

        KeyEnum mapped = MapKey(key);
        if (action == GLFW_PRESS)
        {
            InputManager::SetKeyState(mapped, true);
        }
        else if (action == GLFW_RELEASE)
        {
            InputManager::SetKeyState(mapped, false);
        }
        else
        {
            //GLFW_REPEAT 只更新原始键，不生成新的事件。
            InputManager::SetRawKeyState(static_cast<uint32>(key), true);
            return;
        }

        InputManager::SetRawKeyState(static_cast<uint32>(key), action == GLFW_PRESS);
        InputEvent event;
        event.kind = action == GLFW_PRESS ? KindKeyDown : KindKeyUp;
        event.device = DeviceKeyboard;
        event.key = static_cast<uint32>(mapped);
        event.rawKey = static_cast<uint32>(key);
        event.modifiers = ModifierBits(mods);
        event.position = CurrentPointerPosition();
        event.timestamp = glfwGetTime();
        InputManager::PushEvent(std::move(event));
    }

    void MouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods)
    {
        (void)glfwWindow;
        (void)mods;

        KeyEnum mapped = MapMouseButton(button);
        if (action != GLFW_PRESS && action != GLFW_RELEASE) return;
        InputManager::SetKeyState(mapped, action == GLFW_PRESS);

        InputEvent event;
        event.kind = action == GLFW_PRESS ? KindPointerDown : KindPointerUp;
        event.device = DeviceMouse;
        event.key = PointerButtonOf(mapped);
        event.rawKey = static_cast<uint32>(button);
        event.modifiers = ModifierBits(mods);
        event.position = CurrentPointerPosition();
        event.timestamp = glfwGetTime();
        InputManager::PushEvent(std::move(event));
    }

    void CursorPosCallback(GLFWwindow* glfwWindow, double x, double y)
    {
        (void)glfwWindow;
        vector2 previous = InputManager::MousePos();
        vector2 position(static_cast<float32>(x), static_cast<float32>(y));
        InputManager::SetMousePosition(position.x, position.y);

        InputEvent event;
        event.kind = KindPointerMove;
        event.device = DeviceMouse;
        event.position = position;
        event.delta = vector2(position.x - previous.x, position.y - previous.y);
        event.timestamp = glfwGetTime();
        InputManager::PushEvent(std::move(event));
    }

    //滚轮：x 为横向、y 为纵向；事件按纵向一次记录，带原始两轴数值。
    void ScrollCallback(GLFWwindow* glfwWindow, double xOffset, double yOffset)
    {
        (void)glfwWindow;
        InputEvent event;
        event.kind = KindWheel;
        event.device = DeviceMouse;
        event.position = CurrentPointerPosition();
        event.delta = vector2(static_cast<float32>(xOffset), static_cast<float32>(yOffset));
        event.value = static_cast<float32>(yOffset);
        event.timestamp = glfwGetTime();
        InputManager::PushEvent(std::move(event));
    }

    //字符回调只生成文本提交，不从键码推字符。
    void CharCallback(GLFWwindow* glfwWindow, unsigned int codepoint)
    {
        (void)glfwWindow;
        if (codepoint == 0) return;

        InputEvent event;
        event.kind = KindTextCommit;
        event.device = DeviceKeyboard;
        event.position = CurrentPointerPosition();
        //UTF-8 编码；四个字节足够容纳一个码点。
        char buffer[5] = {};
        int length = 0;
        if (codepoint < 0x80)
        {
            buffer[length++] = static_cast<char>(codepoint);
        }
        else if (codepoint < 0x800)
        {
            buffer[length++] = static_cast<char>(0xC0 | (codepoint >> 6));
            buffer[length++] = static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint < 0x10000)
        {
            buffer[length++] = static_cast<char>(0xE0 | (codepoint >> 12));
            buffer[length++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            buffer[length++] = static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else
        {
            buffer[length++] = static_cast<char>(0xF0 | (codepoint >> 18));
            buffer[length++] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            buffer[length++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            buffer[length++] = static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        event.text.assign(buffer, static_cast<usize>(length));
        event.timestamp = glfwGetTime();
        InputManager::PushEvent(std::move(event));
    }

    //失焦：生成一条事件让 UI 放手全部占有，按着的键继续屏蔽到抬起。
    void WindowFocusCallback(GLFWwindow* glfwWindow, int focused)
    {
        (void)glfwWindow;
        if (focused) return;

        InputEvent event;
        event.kind = KindWindowFocusLost;
        event.position = CurrentPointerPosition();
        event.timestamp = glfwGetTime();
        InputManager::PushEvent(std::move(event));
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

//销毁 GLFW 窗口
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
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);

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
            StopGlfwTimer();
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
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetCharCallback(window, CharCallback);
    glfwSetWindowFocusCallback(window, WindowFocusCallback);

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
        StopGlfwTimer();
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

//阻塞等待 GLFW 事件
void GlfwWindow::WaitEvents()
{
    if (!window) return;

    glfwWaitEvents();
}

//阻塞等待 GLFW 事件，最多等 seconds 秒
void GlfwWindow::WaitEventsTimeout(float64 seconds)
{
    if (!window) return;

    glfwWaitEventsTimeout(seconds > 0.0 ? seconds : 0.0);
}

//唤醒等待中的 GLFW 事件循环
void GlfwWindow::WakeEventLoop()
{
    glfwPostEmptyEvent();
}

//提交当前帧显示
void GlfwWindow::Present()
{
    if (!window) return;

    if (desc.graphicsApi == WindowGraphicsApi::OpenGL)
    {
        const bool capturing = Profiler::IsCapturing();
        const bool currentContext = glfwGetCurrentContext() == window;
        //记录录制开始时的呈现环境
        if (capturing && !wasCapturingPresent)
        {
            int32 swapInterval = -999;
#ifdef _WIN32
            using GetSwapInterval = int32 (__stdcall*)();
            auto getSwapInterval = reinterpret_cast<GetSwapInterval>(glfwGetProcAddress("wglGetSwapIntervalEXT"));
            if (currentContext && getSwapInterval) swapInterval = getSwapInterval();
#endif
            char message[768];
            const GLubyte* renderer = currentContext && glGetString ? glGetString(GL_RENDERER) : nullptr;
            snprintf(message, sizeof(message),
                "Present diagnostic: GPU=%s framebuffer=%dx%d requestedVsync=%d swapInterval=%d "
                "focused=%d iconified=%d currentContext=%d. Capture inserts glFlush/glFinish before swap; "
                "WaitForGpu is CPU blocking time, not total GPU execution time. swapInterval=-999 means unavailable.",
                renderer ? reinterpret_cast<const char*>(renderer) : "unavailable", framebufferWidth, framebufferHeight,
                desc.vsync ? 1 : 0, swapInterval, glfwGetWindowAttrib(window, GLFW_FOCUSED),
                glfwGetWindowAttrib(window, GLFW_ICONIFIED), currentContext ? 1 : 0);
            Log::Info(message);
        }
        wasCapturingPresent = capturing;

        PROFILE("Render/SwapBuffers");
        //仅在录制期间拆开已提交绘制的等待与交换缓冲
        if (capturing && currentContext && glFlush && glFinish)
        {
            {
                PROFILE("Render/SwapBuffers/FlushCommands");
                glFlush();
            }
            {
                PROFILE("Render/SwapBuffers/WaitForGpu");
                glFinish();
            }
        }
        {
            PROFILE("Render/SwapBuffers/Present");
            glfwSwapBuffers(window);
        }
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

//获取 GLFW 窗口指针
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
