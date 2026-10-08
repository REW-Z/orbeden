#pragma once

#include "Defines/types.h"
#include "Platform/WindowsInputHook.h"

//Windows 触摸输入。触摸走 WM_POINTER，并吞掉系统为触摸合成的兼容鼠标消息：
//不这么做的话，一次触摸会既被当成触摸又被当成鼠标，UI 上表现为重复点击。
class WindowsPointerInput : public IWindowsMessageHandler
{
public:
    //挂在统一的窗口子类入口上。
    bool Attach(void* nativeHandle);

    //摘掉子类登记并丢弃指针映射。
    void Detach();

    //IWindowsMessageHandler
    bool OnWindowMessage(void* hwnd, uint32 message, uintptr wParam, intptr lParam) override;

private:
    //Win32 指针 ID 到触摸 ID 的映射；触摸 ID 从 1 开始，0 留给鼠标。
    struct PointerMapping
    {
        uint32 nativeId = 0;
        uint32 pointerId = 0;
    };

    void* window = nullptr;
    PointerMapping mappings[16] = {};
    int32 mappingCount = 0;
    uint32 nextPointerId = 1;
    //兼容鼠标的屏蔽截止时刻；触摸期间与抬手后一小段都吞掉鼠标消息。
    uint64 suppressMouseUntil = 0;

    //把 Win32 指针 ID 映射成触摸 ID；新指针分配下一个号。
    uint32 MapPointer(uint32 nativeId, bool create);
    //释放一个指针映射。
    void ReleasePointer(uint32 nativeId);
    //推进屏蔽窗口。
    void ExtendMouseSuppression();
};

namespace WindowsPointerInputDetail
{
    //触摸事件处理器；进程内只有一个，供桥接层与窗口过程共用。
    WindowsPointerInput& Instance();
}
