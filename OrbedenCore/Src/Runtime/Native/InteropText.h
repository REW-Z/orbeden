#pragma once

#include "Defines/types.h"

#include <filesystem>
#include <string>
#include <string_view>

//统一处理原生文本与文件系统字符表示。
namespace InteropText
{
    //将已校验的 Unicode 码点追加为 UTF-8 字节
    void AppendUtf8Codepoint(std::string& text, uint32 codepoint);

    //从 UTF-8 字节构造原生文件系统路径
    std::filesystem::path PathFromUtf8(std::string_view text);

    //读取采用正斜杠的 UTF-8 路径
    std::string PathToUtf8(const std::filesystem::path& path);

    //读取保留平台分隔符的 UTF-8 路径
    std::string PathToNativeUtf8(const std::filesystem::path& path);

    //读取原生宽字符路径
    std::wstring PathToWide(const std::filesystem::path& path);

    //从宽字符构造原生文件系统路径
    std::filesystem::path PathFromWide(const wchar_t* text);

    //通过文件系统规则转换 UTF-8 到宽字符
    std::wstring Utf8ToWide(std::string_view text);

#ifdef _WIN32
    //严格转换 UTF-16 到 UTF-8，非法序列返回假
    bool TryUtf16ToUtf8(const wchar_t* text, int32 length, std::string& output);

    //严格转换 UTF-8 到 UTF-16，非法序列返回假
    bool TryUtf8ToUtf16(std::string_view text, std::wstring& output);
#endif
}
