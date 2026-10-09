#include "Runtime/Native/InteropText.h"

#include <cstring>
#include <limits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

//将已校验的 Unicode 码点追加为 UTF-8 字节
void InteropText::AppendUtf8Codepoint(std::string& text, uint32 codepoint)
{
    char buffer[4];
    usize length = 0;
    if (codepoint < 0x80) buffer[length++] = static_cast<char>(codepoint);
    else if (codepoint < 0x800)
    {
        buffer[length++] = static_cast<char>(0xc0 | (codepoint >> 6));
        buffer[length++] = static_cast<char>(0x80 | (codepoint & 0x3f));
    }
    else if (codepoint < 0x10000)
    {
        buffer[length++] = static_cast<char>(0xe0 | (codepoint >> 12));
        buffer[length++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        buffer[length++] = static_cast<char>(0x80 | (codepoint & 0x3f));
    }
    else
    {
        buffer[length++] = static_cast<char>(0xf0 | (codepoint >> 18));
        buffer[length++] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
        buffer[length++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        buffer[length++] = static_cast<char>(0x80 | (codepoint & 0x3f));
    }
    text.append(buffer, length);
}

//从 UTF-8 字节构造原生文件系统路径
std::filesystem::path InteropText::PathFromUtf8(std::string_view text)
{
    std::u8string bytes(text.size(), u8'\0');
    if (!text.empty()) std::memcpy(bytes.data(), text.data(), text.size());
    return std::filesystem::path(bytes);
}

//读取采用正斜杠的 UTF-8 路径
std::string InteropText::PathToUtf8(const std::filesystem::path& path)
{
    std::u8string bytes = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

//读取保留平台分隔符的 UTF-8 路径
std::string InteropText::PathToNativeUtf8(const std::filesystem::path& path)
{
    std::u8string bytes = path.u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

//读取原生宽字符路径
std::wstring InteropText::PathToWide(const std::filesystem::path& path)
{
    return path.wstring();
}

//从宽字符构造原生文件系统路径
std::filesystem::path InteropText::PathFromWide(const wchar_t* text)
{
    return text ? std::filesystem::path(text) : std::filesystem::path();
}

//通过文件系统规则转换 UTF-8 到宽字符
std::wstring InteropText::Utf8ToWide(std::string_view text)
{
    return PathToWide(PathFromUtf8(text));
}

#ifdef _WIN32
//严格转换 UTF-16 到 UTF-8
bool InteropText::TryUtf16ToUtf8(const wchar_t* text, int32 length, std::string& output)
{
    output.clear();
    if (length <= 0) return true;
    int32 bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return false;
    output.resize(static_cast<usize>(bytes));
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, output.data(), bytes, nullptr, nullptr) == bytes;
}

//严格转换 UTF-8 到 UTF-16
bool InteropText::TryUtf8ToUtf16(std::string_view text, std::wstring& output)
{
    output.clear();
    if (text.empty()) return true;
    if (text.size() > static_cast<usize>(std::numeric_limits<int32>::max())) return false;
    int32 length = static_cast<int32>(text.size());
    int32 count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (count <= 0) return false;
    output.resize(static_cast<usize>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, output.data(), count) == count;
}
#endif
