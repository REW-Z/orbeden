#pragma once

#include <string>

#include "Defines/types.h"
#include "Platform/WindowsInputHook.h"

//Windows 文本输入：一条窗口子类入口同时负责输入法组合与剪贴板。
//普通字符仍走 GLFW 的字符回调（只生成 TextCommit，不从键码推字符），
//这里只处理输入法消息、已提交内容的兼容字符去重，以及剪贴板读写。
//进程内只应有一个活动实例：文本焦点本来就是全局唯一的。
class WindowsTextInput : public IWindowsMessageHandler
{
public:
    //挂上窗口子类；nativeHandle 是平台原生窗口句柄。重复挂载同一个窗口无副作用。
    bool Attach(void* nativeHandle);

    //摘掉子类并取消当前组合。
    void Detach();

    //设置文本输入是否活跃；不活跃时不接收输入法消息。
    void SetActive(bool value);

    //取消当前的输入法组合。
    void CancelComposition();

    //设置光标矩形；坐标是窗口客户区逻辑坐标，供输入法候选窗定位。
    void SetCaretRect(int32 x, int32 y, int32 width, int32 height);

    //设置文本会话标识；之后产生的组合事件都带上它，离焦后的旧提交由上层丢弃。
    void SetTextSession(uint64 session);

    //读取剪贴板文本，编码为 UTF-8；失败时不修改 out。
    bool ReadClipboard(std::string& out);

    //写入 UTF-8 文本到剪贴板；失败返回假。
    bool WriteClipboard(const std::string& text);

    //当前是否已经挂上窗口。
    bool IsAttached() const;

    //IWindowsMessageHandler：返回真表示这条消息已经处理完，不再转给原窗口过程。
    bool OnWindowMessage(void* hwnd, uint32 message, uintptr wParam, intptr lParam) override;

private:
    void* window = nullptr;
    //保存原始输入法上下文，SetActive(false) 时摘掉、恢复时装回。
    void* savedContext = nullptr;
    bool active = true;
    bool composing = false;
    //本次组合是否已经提交过结果：提交之后的结束不算取消。
    bool committed = false;
    //已提交但还等着被兼容字符消息回显的内容；匹配上就吞掉，不重复生成字符。
    std::string pendingEcho;
    uint64 textSession = 0;
    int32 caretX = 0;
    int32 caretY = 0;
    int32 caretWidth = 0;
    int32 caretHeight = 0;

    //把光标矩形写进输入法的组合窗口与候选窗口。
    void ApplyCaretRect();
    //推一条文本或组合事件；带上当前会话标识。
    void PushEvent(uint32 kind, const std::string& text, uint32 caretScalar);
    //已提交内容的兼容字符去重：匹配上返回真并消耗一个字符。
    bool ConsumeEcho(uint32 codepoint);
};

namespace WindowsTextInputDetail
{
    //文本输入的进程内实例：窗口、应用与 ABI 桥接共用同一个。
    WindowsTextInput& Instance();
}
