#include "Runtime/DisplaySettings.h"

#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    float32 exposure = DisplaySettings::DefaultExposure;
    std::string loadedRoot;
    std::filesystem::file_time_type loadedTime;
    bool initialized = false;

    //解析一行 "键\t值"，键不认识就跳过，便于以后追加参数。
    bool ApplyLine(const std::string& line)
    {
        usize separator = line.find('\t');
        if (separator == std::string::npos) return false;

        std::string_view key(line.data(), separator);
        if (key != "exposure") return false;

        float32 parsed = DisplaySettings::DefaultExposure;
        const char* begin = line.data() + separator + 1;
        const char* end = line.data() + line.size();
        auto result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc() || result.ptr != end || !(parsed > 0.0f)) return false;

        exposure = parsed;
        return true;
    }
}

//仅在内容根或文件时间变化时解析，避免重复读取配置内容。
void DisplaySettings::Refresh()
{
    const std::string& root = PathDefines::GetContentRoot();
    std::error_code error;
    auto path = Utf8Path::FromUtf8(PathDefines::GetContentFilePath(FileName));
    auto modified = std::filesystem::last_write_time(path, error);
    if (initialized && root == loadedRoot && modified == loadedTime) return;
    initialized = true;
    loadedRoot = root;
    loadedTime = modified;
    exposure = DefaultExposure;
    if (root.empty() || error) return;

    std::ifstream input(path);
    std::string line;
    if (!std::getline(input, line) || line != "OrbedenDisplay1")
    {
        Log::Error("Invalid ProjectSettings.display; using the default exposure.");
        return;
    }

    while (std::getline(input, line))
    {
        if (line.empty()) continue;
        if (!ApplyLine(line))
        {
            Log::Error("Ignored an unsupported line in ProjectSettings.display.");
        }
    }
}

//获取线性曝光倍数
float32 DisplaySettings::GetExposure()
{
    Refresh();
    return exposure;
}
