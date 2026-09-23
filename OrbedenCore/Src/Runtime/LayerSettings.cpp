#include "Runtime/LayerSettings.h"

#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"

#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    std::array<uint32, 32> masks = [] { std::array<uint32, 32> value; value.fill(0xFFFFFFFFu); return value; }();
    std::string loadedRoot;
    std::filesystem::file_time_type loadedTime;
    bool initialized = false;
}

//仅在内容根或文件时间变化时解析，避免重复读取配置内容。
void LayerSettings::Refresh()
{
    const std::string& root = PathDefines::GetContentRoot();
    std::error_code error;
    auto path = Utf8Path::FromUtf8(PathDefines::GetContentFilePath(FileName));
    auto modified = std::filesystem::last_write_time(path, error);
    if (initialized && root == loadedRoot && modified == loadedTime) return;
    initialized = true;
    loadedRoot = root;
    loadedTime = modified;
    masks.fill(0xFFFFFFFFu);
    if (root.empty() || error) return;

    std::ifstream input(path);
    std::string line;
    bool valid = static_cast<bool>(std::getline(input, line)) && line == "OrbedenLayers1";
    std::array<uint32, 32> parsed{};
    for (uint32 index = 0; valid && index < 32; ++index)
    {
        valid = static_cast<bool>(std::getline(input, line));
        usize separator = line.find('\t');
        if (!valid || separator != 8) { valid = false; break; }
        auto result = std::from_chars(line.data(), line.data() + separator, parsed[index], 16);
        valid = result.ec == std::errc() && result.ptr == line.data() + separator;
    }
    if (valid && std::getline(input, line)) valid = false;
    for (uint32 row = 0; valid && row < 32; ++row)
        for (uint32 column = 0; column < 32; ++column)
            if (((parsed[row] >> column) & 1u) != ((parsed[column] >> row) & 1u)) valid = false;
    if (valid) masks = parsed;
    else Log::Error("Invalid ProjectSettings.layers; using the default collision matrix.");
}

//旧场景可能保存多个位，按这些层允许的目标取并集。
uint32 LayerSettings::GetCollisionMask(uint32 layers)
{
    uint32 result = 0;
    for (uint32 index = 0; index < 32; ++index)
        if ((layers & (1u << index)) != 0) result |= masks[index];
    return result;
}
