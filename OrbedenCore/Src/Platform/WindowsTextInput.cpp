#include "Platform/WindowsTextInput.h"

#ifdef _WIN32

//windows.h 会拖进一堆宏；这个 TU 只做平台消息，包含顺序无所谓。
//WIN32_LEAN_AND_MEAN 挡掉 rpcndr.h：它会把 byte 重定义成另一种类型。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>

#include <cstring>
#include <string>

#include "InputManager/InputEvent.h"
#include "Platform/WindowsInputHook.h"
#include "InputManager/InputManager.h"
#include "Log/Log.h"

namespace
{
    //事件种类与设备；与 UIInputKind/UIInputDevice 的数值一致。
    constexpr uint32 EventKindTextCommit = 7;
    constexpr uint32 EventKindCompositionStart = 8;
    constexpr uint32 EventKindCompositionUpdate = 9;
    constexpr uint32 EventKindCompositionCommit = 10;
    constexpr uint32 EventKindCompositionCancel = 11;
    constexpr uint32 DeviceKeyboard = 3;

    WindowsTextInput* activeInstance = nullptr;

    //UTF-16 转 UTF-8；遇到非法序列返回假，不做截断。
    bool ToUtf8(const wchar_t* text, int32 length, std::string& out)
    {
        out.clear();
        if (length <= 0) return true;

        int32 bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) return false;

        out.resize(static_cast<usize>(bytes));
        return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length,
            out.data(), bytes, nullptr, nullptr) == bytes;
    }

    //UTF-8 转 UTF-16；遇到非法序列返回假，不做截断。
    bool ToUtf16(const std::string& text, std::wstring& out)
    {
        out.clear();
        if (text.empty()) return true;

        int32 count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int32>(text.size()), nullptr, 0);
        if (count <= 0) return false;

        out.resize(static_cast<usize>(count));
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int32>(text.size()), out.data(), count) == count;
    }

    //读取输入法组合串；没有内容时返回空串。
    std::string ReadCompositionString(HIMC context, DWORD kind)
    {
        LONG bytes = ImmGetCompositionStringW(context, kind, nullptr, 0);
        if (bytes <= 0) return std::string();

        std::wstring buffer(static_cast<usize>(bytes) / sizeof(wchar_t), L'\0');
        LONG written = ImmGetCompositionStringW(context, kind, buffer.data(), bytes);
        if (written <= 0) return std::string();

        buffer.resize(static_cast<usize>(written) / sizeof(wchar_t));
        std::string utf8;
        if (!ToUtf8(buffer.data(), static_cast<int32>(buffer.size()), utf8)) return std::string();
        return utf8;
    }

}

//进程内唯一的实例；两个分支共用，所以放在条件编译之外。
static WindowsTextInput textInputInstance;

WindowsTextInput& WindowsTextInputDetail::Instance()
{
    return textInputInstance;
}

bool WindowsTextInput::Attach(void* nativeHandle)
{
    if (!nativeHandle) return false;
    if (window == nativeHandle) return true;
    if (window) Detach();

    //挂在统一的窗口子类入口上：一个窗口只能有一个替换过程。
    if (!WindowsInputHook::Attach(nativeHandle, this)) return false;

    window = nativeHandle;
    activeInstance = this;
    active = true;
    composing = false;
    committed = false;
    pendingEcho.clear();
    return true;
}

void WindowsTextInput::Detach()
{
    if (!window) return;

    CancelComposition();
    //把输入法上下文装回去再摘钩子，避免状态留在别的窗口过程上。
    if (savedContext) SetActive(true);
    WindowsInputHook::Detach(this);

    if (activeInstance == this) activeInstance = nullptr;
    window = nullptr;
    pendingEcho.clear();
}

bool WindowsTextInput::IsAttached() const
{
    return window != nullptr;
}

void WindowsTextInput::SetActive(bool value)
{
    if (!window || active == value) return;

    HWND hwnd = static_cast<HWND>(window);
    if (!value)
    {
        CancelComposition();
        //摘掉输入法上下文：不活跃时连候选窗都不该出现。
        savedContext = ImmAssociateContext(hwnd, nullptr);
    }
    else if (savedContext)
    {
        ImmAssociateContext(hwnd, static_cast<HIMC>(savedContext));
        savedContext = nullptr;
    }
    active = value;
}

void WindowsTextInput::CancelComposition()
{
    if (!window || !composing) return;

    HWND hwnd = static_cast<HWND>(window);
    HIMC context = ImmGetContext(hwnd);
    if (context)
    {
        ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmReleaseContext(hwnd, context);
    }

    composing = false;
    committed = false;
    pendingEcho.clear();
    PushEvent(EventKindCompositionCancel, std::string(), 0);
}

void WindowsTextInput::SetCaretRect(int32 x, int32 y, int32 width, int32 height)
{
    caretX = x;
    caretY = y;
    caretWidth = width;
    caretHeight = height;
    ApplyCaretRect();
}

void WindowsTextInput::SetTextSession(uint64 session)
{
    textSession = session;
}

void WindowsTextInput::ApplyCaretRect()
{
    if (!window) return;

    HWND hwnd = static_cast<HWND>(window);
    HIMC context = ImmGetContext(hwnd);
    if (!context) return;

    //候选窗与组合窗都按客户区逻辑坐标定位；高度至少留一行，避免贴边。
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_RECT;
    composition.ptCurrentPos.x = caretX;
    composition.ptCurrentPos.y = caretY;
    composition.rcArea.left = caretX;
    composition.rcArea.top = caretY;
    composition.rcArea.right = caretX + (caretWidth > 0 ? caretWidth : 1);
    composition.rcArea.bottom = caretY + (caretHeight > 0 ? caretHeight : 1);
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos.x = caretX;
    candidate.ptCurrentPos.y = caretY + (caretHeight > 0 ? caretHeight : 1);
    ImmSetCandidateWindow(context, &candidate);

    ImmReleaseContext(hwnd, context);
}

bool WindowsTextInput::ReadClipboard(std::string& out)
{
    if (!window) return false;

    HWND hwnd = static_cast<HWND>(window);
    if (!OpenClipboard(hwnd)) return false;

    bool success = false;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle)
    {
        const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
        if (text)
        {
            std::string utf8;
            success = ToUtf8(text, static_cast<int32>(wcslen(text)), utf8);
            //读取失败时不动 out，调用方保留原文。
            if (success) out = std::move(utf8);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return success;
}

bool WindowsTextInput::WriteClipboard(const std::string& text)
{
    if (!window) return false;

    std::wstring wide;
    if (!ToUtf16(text, wide))
    {
        Log::Error("WindowsTextInput: clipboard text is not valid UTF-8.");
        return false;
    }

    HWND hwnd = static_cast<HWND>(window);
    if (!OpenClipboard(hwnd)) return false;

    bool success = false;
    if (EmptyClipboard())
    {
        //全局内存交给剪贴板持有，成功后不能再释放。
        SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory)
        {
            void* target = GlobalLock(memory);
            if (target)
            {
                std::memcpy(target, wide.c_str(), bytes);
                GlobalUnlock(memory);
                success = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
            }
            if (!success) GlobalFree(memory);
        }
    }
    CloseClipboard();
    return success;
}

void WindowsTextInput::PushEvent(uint32 kind, const std::string& text, uint32 caretScalar)
{
    InputEvent event;
    event.kind = kind;
    event.device = DeviceKeyboard;
    event.text = text;
    event.caretScalar = caretScalar;
    event.position = InputManager::MousePos();
    event.timestamp = static_cast<double>(GetTickCount64()) / 1000.0;
    InputManager::PushEvent(std::move(event));
}

bool WindowsTextInput::ConsumeEcho(uint32 codepoint)
{
    if (pendingEcho.empty()) return false;

    //按 UTF-8 取出队首码点比较；相同就吞掉这一个字符。
    usize length = 1;
    unsigned char lead = static_cast<unsigned char>(pendingEcho[0]);
    if ((lead & 0xE0) == 0xC0) length = 2;
    else if ((lead & 0xF0) == 0xE0) length = 3;
    else if ((lead & 0xF8) == 0xF0) length = 4;
    if (length > pendingEcho.size()) length = pendingEcho.size();

    std::string head = pendingEcho.substr(0, length);
    std::wstring wide;
    if (!ToUtf16(head, wide) || wide.size() != 1 || static_cast<uint32>(wide[0]) != codepoint)
    {
        //对不上说明是新的输入，回显作废，正常放行。
        pendingEcho.clear();
        return false;
    }

    pendingEcho.erase(0, length);
    return true;
}

bool WindowsTextInput::OnWindowMessage(void* hwndPointer, uint32 message, uintptr wParam, intptr lParam)
{
    if (!window || static_cast<void*>(hwndPointer) != window) return false;

    HWND hwnd = static_cast<HWND>(window);
    switch (message)
    {
    case WM_IME_SETCONTEXT:
        //组合串由应用自己画，屏蔽系统默认的组合窗。
        lParam &= ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW);
        return false;

    case WM_IME_STARTCOMPOSITION:
        composing = true;
        committed = false;
        pendingEcho.clear();
        ApplyCaretRect();
        PushEvent(EventKindCompositionStart, std::string(), 0);
        return true;

    case WM_IME_COMPOSITION:
    {
        HIMC context = ImmGetContext(hwnd);
        if (!context) return false;

        if (lParam & GCS_RESULTSTR)
        {
            std::string committedText = ReadCompositionString(context, GCS_RESULTSTR);
            composing = false;
            committed = true;
            //记下回显：接下来同内容的兼容字符消息要吞掉，不能重复上报。
            pendingEcho = committedText;
            PushEvent(EventKindCompositionCommit, committedText, 0);
        }
        else if (lParam & GCS_COMPSTR)
        {
            std::string text = ReadCompositionString(context, GCS_COMPSTR);
            LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
            PushEvent(EventKindCompositionUpdate, text, cursor > 0 ? static_cast<uint32>(cursor) : 0);
        }

        ImmReleaseContext(hwnd, context);
        return true;
    }

    case WM_IME_ENDCOMPOSITION:
        if (composing)
        {
            //没有提交结果的结束就是取消。
            composing = false;
            PushEvent(EventKindCompositionCancel, std::string(), 0);
        }
        committed = false;
        return true;

    case WM_IME_CHAR:
    case WM_CHAR:
        //输入法提交后的兼容字符消息：与已提交内容一致就吞掉。
        if (ConsumeEcho(static_cast<uint32>(wParam))) return true;
        return false;

    case WM_KILLFOCUS:
        CancelComposition();
        return false;

    default:
        return false;
    }
}

#else

//非 Windows 平台没有输入法窗口消息；接口保留为空实现。
bool WindowsTextInput::Attach(void*) { return false; }
void WindowsTextInput::Detach() {}
void WindowsTextInput::SetActive(bool) {}
void WindowsTextInput::CancelComposition() {}
void WindowsTextInput::SetCaretRect(int32, int32, int32, int32) {}
void WindowsTextInput::SetTextSession(uint64) {}
bool WindowsTextInput::ReadClipboard(std::string&) { return false; }
bool WindowsTextInput::WriteClipboard(const std::string&) { return false; }
bool WindowsTextInput::IsAttached() const { return false; }
bool WindowsTextInput::OnWindowMessage(void*, uint32, uintptr, intptr) { return false; }

#endif
