#include "Platform/WindowsPointerInput.h"

#ifdef _WIN32

//WIN32_LEAN_AND_MEAN 挡掉 rpcndr.h：它会把 byte 重定义成另一种类型。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "InputManager/InputEvent.h"
#include "InputManager/InputManager.h"
#include "Log/Log.h"

//进程内唯一的实例；两个分支共用，所以放在条件编译之外。
static WindowsPointerInput instance;

namespace
{
    //事件种类与设备；与 UIInputKind/UIInputDevice 的数值一致。
    constexpr uint32 EventKindPointerMove = 0;
    constexpr uint32 EventKindPointerDown = 1;
    constexpr uint32 EventKindPointerUp = 2;
    constexpr uint32 EventKindPointerCancel = 3;
    constexpr uint32 DeviceTouch = 1;
    //触摸固定按左键。
    constexpr uint32 PointerButtonLeft = 0;

    //触摸抬起后继续屏蔽兼容鼠标的时长；系统可能把最后的抬起也合成为鼠标消息。
    constexpr uint64 MouseSuppressionAfterTouch = 120;

    uint64 NowMilliseconds()
    {
        return GetTickCount64();
    }
}

WindowsPointerInput& WindowsPointerInputDetail::Instance()
{
    return instance;
}

bool WindowsPointerInput::Attach(void* nativeHandle)
{
    if (!nativeHandle) return false;
    if (window == nativeHandle) return true;

    if (!WindowsInputHook::Attach(nativeHandle, this)) return false;
    window = nativeHandle;
    mappingCount = 0;
    nextPointerId = 1;
    suppressMouseUntil = 0;
    return true;
}

void WindowsPointerInput::Detach()
{
    if (!window) return;

    if (mappingCount > 0)
    {
        //还有手指按着就取消掉，避免上层留下永远不抬起的指针。
        for (int32 index = 0; index < mappingCount; ++index)
        {
            InputEvent event;
            event.kind = EventKindPointerCancel;
            event.device = DeviceTouch;
            event.pointerId = mappings[index].pointerId;
            event.key = PointerButtonLeft;
            InputManager::PushEvent(std::move(event));
        }
    }

    WindowsInputHook::Detach(this);
    window = nullptr;
    mappingCount = 0;
    suppressMouseUntil = 0;
}

uint32 WindowsPointerInput::MapPointer(uint32 nativeId, bool create)
{
    for (int32 index = 0; index < mappingCount; ++index)
    {
        if (mappings[index].nativeId == nativeId) return mappings[index].pointerId;
    }
    if (!create || mappingCount >= static_cast<int32>(sizeof(mappings) / sizeof(mappings[0]))) return 0;

    mappings[mappingCount].nativeId = nativeId;
    mappings[mappingCount].pointerId = nextPointerId++;
    ++mappingCount;
    return mappings[mappingCount - 1].pointerId;
}

void WindowsPointerInput::ReleasePointer(uint32 nativeId)
{
    for (int32 index = 0; index < mappingCount; ++index)
    {
        if (mappings[index].nativeId != nativeId) continue;
        //用尾元素填补空位，映射表顺序不重要。
        mappings[index] = mappings[mappingCount - 1];
        --mappingCount;
        return;
    }
}

void WindowsPointerInput::ExtendMouseSuppression()
{
    suppressMouseUntil = NowMilliseconds() + MouseSuppressionAfterTouch;
}

bool WindowsPointerInput::OnWindowMessage(void* hwndPointer, uint32 message, uintptr wParam, intptr lParam)
{
    if (!window || static_cast<void*>(hwndPointer) != window) return false;

    HWND hwnd = static_cast<HWND>(window);
    switch (message)
    {
    case WM_POINTERUPDATE:
    case WM_POINTERDOWN:
    case WM_POINTERUP:
    {
        const uint32 nativeId = static_cast<uint32>(GET_POINTERID_WPARAM(wParam));
        POINTER_INFO info{};
        if (!GetPointerInfo(nativeId, &info)) return false;

        //只接管触摸与笔：鼠标仍走 GLFW 的鼠标路径，避免两套坐标口径打架。
        const bool touch = info.pointerType == PT_TOUCH || info.pointerType == PT_PEN;
        if (!touch) return false;

        //坐标换算到窗口客户区逻辑坐标。
        POINT position = info.ptPixelLocation;
        ScreenToClient(hwnd, &position);

        uint32 pointerId = MapPointer(nativeId, true);
        if (pointerId == 0) return true;

        InputEvent event;
        event.device = DeviceTouch;
        event.pointerId = pointerId;
        event.key = PointerButtonLeft;
        event.position = vector2(static_cast<float32>(position.x), static_cast<float32>(position.y));
        //POINTER_INFO 不带压力；触摸压力要另取 POINTER_TOUCH_INFO，这里不参与 UI 判定。
        event.value = 0.0f;
        event.timestamp = static_cast<double>(info.PerformanceCount) / 1.0e7;

        if (message == WM_POINTERDOWN)
        {
            event.kind = EventKindPointerDown;
        }
        else if (message == WM_POINTERUP)
        {
            event.kind = EventKindPointerUp;
            ReleasePointer(nativeId);
        }
        else
        {
            event.kind = EventKindPointerMove;
        }

        ExtendMouseSuppression();
        InputManager::PushEvent(std::move(event));
        //触摸不再转给后续过程：GLFW 不认识 WM_POINTER，转过去只会空转。
        return true;
    }

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        //触摸刚发生过：这些是系统合成的兼容鼠标消息，吞掉以免重复处理。
        if (NowMilliseconds() < suppressMouseUntil) return true;
        return false;

    default:
        return false;
    }
}

#else

bool WindowsPointerInput::Attach(void*) { return false; }
void WindowsPointerInput::Detach() {}
bool WindowsPointerInput::OnWindowMessage(void*, uint32, uintptr, intptr) { return false; }
WindowsPointerInput& WindowsPointerInputDetail::Instance() { return instance; }

#endif
