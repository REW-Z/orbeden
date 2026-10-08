#pragma once

#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

// 显式转换 UTF-8 文本与文件系统原生路径，避免依赖系统本地代码页。
namespace Utf8Path
{
    // 从 UTF-8 字节创建文件系统路径。
    inline std::filesystem::path FromUtf8(std::string_view value)
    {
        std::u8string bytes;
        bytes.resize(value.size());
        if (!value.empty())
        {
            std::memcpy(bytes.data(), value.data(), value.size());
        }

        return std::filesystem::path(bytes);
    }

    // 把文件系统路径转换为使用正斜杠的 UTF-8 文本。
    inline std::string ToUtf8(const std::filesystem::path& path)
    {
        std::u8string bytes = path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
}
