#pragma once

#include "Defines/types.h"

//统一窗口子类入口。文本输入与指针输入都挂在同一条子类上：
//一个窗口只能有一个替换过程，分头挂钩会互相覆盖，也没法保证转发顺序。
class IWindowsMessageHandler
{
public:
    virtual ~IWindowsMessageHandler() = default;

    //处理一条窗口消息；返回真表示已经处理完，不再转给原窗口过程。
    virtual bool OnWindowMessage(void* hwnd, uint32 message, uintptr wParam, intptr lParam) = 0;
};

namespace WindowsInputHook
{
    //挂上子类并登记一个处理器；同一个窗口重复挂载无副作用。
    //handler 必须在 Detach 之前一直有效。
    bool Attach(void* nativeHandle, IWindowsMessageHandler* handler);

    //注销处理器；最后一个处理器注销时摘掉子类。
    void Detach(IWindowsMessageHandler* handler);

    //当前是否已经挂上窗口子类。
    bool IsAttached();
}
