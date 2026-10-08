#include "Platform/WindowsInputHook.h"

#ifdef _WIN32

//WIN32_LEAN_AND_MEAN 挡掉 rpcndr.h：它会把 byte 重定义成另一种类型。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>

#include "Log/Log.h"

namespace
{
    void* hookedWindow = nullptr;
    WNDPROC previousProc = nullptr;
    std::vector<IWindowsMessageHandler*> handlers;

    LRESULT CALLBACK HookProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        //按登记顺序询问处理器；第一个认领的说了算。
        for (IWindowsMessageHandler* handler : handlers)
        {
            if (handler && handler->OnWindowMessage(hwnd, message, wParam, lParam)) return 0;
        }

        if (previousProc) return CallWindowProcW(previousProc, hwnd, message, wParam, lParam);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool WindowsInputHook::Attach(void* nativeHandle, IWindowsMessageHandler* handler)
{
    if (!nativeHandle || !handler) return false;

    if (hookedWindow != nativeHandle)
    {
        if (hookedWindow) return false;
        HWND hwnd = static_cast<HWND>(nativeHandle);
        previousProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&HookProc)));
        if (!previousProc)
        {
            Log::Error("WindowsInputHook: subclass failed.");
            return false;
        }
        hookedWindow = nativeHandle;
    }

    for (IWindowsMessageHandler* existing : handlers)
    {
        if (existing == handler) return true;
    }
    handlers.push_back(handler);
    return true;
}

void WindowsInputHook::Detach(IWindowsMessageHandler* handler)
{
    for (usize index = 0; index < handlers.size(); ++index)
    {
        if (handlers[index] != handler) continue;
        handlers.erase(handlers.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    if (!handlers.empty() || !hookedWindow) return;

    //最后一个处理器走了：把原窗口过程装回去。
    HWND hwnd = static_cast<HWND>(hookedWindow);
    if (previousProc)
    {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousProc));
        previousProc = nullptr;
    }
    hookedWindow = nullptr;
}

bool WindowsInputHook::IsAttached()
{
    return hookedWindow != nullptr;
}

#else

bool WindowsInputHook::Attach(void*, IWindowsMessageHandler*) { return false; }
void WindowsInputHook::Detach(IWindowsMessageHandler*) {}
bool WindowsInputHook::IsAttached() { return false; }

#endif
