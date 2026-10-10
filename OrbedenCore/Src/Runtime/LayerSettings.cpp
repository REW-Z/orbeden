#include "Runtime/LayerSettings.h"

#include "FileSystem/PathDefines.h"
#include "Runtime/GameSettingsFile.h"
#include "Runtime/Native/InteropText.h"
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

    //碰撞矩阵所在的分块名
    constexpr const char* LayersSection = "Layers";

    //识别 "[分块名]" 行并切换当前分块
    bool ParseSection(const std::string& line, std::string& section)
    {
        if (line.size() < 2 || line.front() != '[' || line.back() != ']') return false;
        section.assign(line, 1, line.size() - 2);
        return true;
    }

    //解析一行 "<序号>\t<层名>\t<掩码>"。层名只有编辑器关心，这里跳过；
    //序号必须落在 0..31，掩码固定八位十六进制。
    bool ApplyLine(const std::string& line, bool (&seen)[32], std::array<uint32, 32>& parsed)
    {
        usize first = line.find('\t');
        if (first == std::string::npos) return false;
        usize second = line.find('\t', first + 1);
        if (second == std::string::npos) return false;

        uint32 index = 0;
        auto indexResult = std::from_chars(line.data(), line.data() + first, index);
        if (indexResult.ec != std::errc() || indexResult.ptr != line.data() + first || index >= 32) return false;

        const char* maskBegin = line.data() + second + 1;
        const char* maskEnd = line.data() + line.size();
        uint32 mask = 0;
        auto maskResult = std::from_chars(maskBegin, maskEnd, mask, 16);
        if (maskResult.ec != std::errc() || maskResult.ptr != maskEnd) return false;

        seen[index] = true;
        parsed[index] = mask;
        return true;
    }
}

//仅在内容根或文件时间变化时解析，避免重复读取配置内容。
void LayerSettings::Refresh()
{
    const std::string& root = PathDefines::GetContentRoot();
    std::filesystem::path path = InteropText::PathFromUtf8(GameSettingsFile::GetPath());
    std::error_code error;
    auto modified = path.empty() ? std::filesystem::file_time_type::min() : std::filesystem::last_write_time(path, error);
    if (initialized && root == loadedRoot && modified == loadedTime) return;
    initialized = true;
    loadedRoot = root;
    loadedTime = modified;
    masks.fill(0xFFFFFFFFu);
    if (root.empty() || error) return;

    std::ifstream input(path);
    std::string line;
    std::string section;
    bool seen[32] = {};
    std::array<uint32, 32> parsed{};
    bool valid = true;
    //分块在不在，和分块写得对不对是两件事：没有这一段就安静地用默认值
    //（编辑器会在读到缺段时按默认值补写），只有写了却写坏才值得报错
    bool hasSection = false;
    while (std::getline(input, line))
    {
        if (line.empty()) continue;
        if (ParseSection(line, section))
        {
            if (section == LayersSection) hasSection = true;
            continue;
        }
        //不认识的块整块跳过：文件里的块可能属于更晚的引擎版本
        if (section != LayersSection) continue;
        if (!ApplyLine(line, seen, parsed))
        {
            valid = false;
            break;
        }
    }

    if (!hasSection) return;
    for (uint32 index = 0; valid && index < 32; ++index) valid = seen[index];
    for (uint32 row = 0; valid && row < 32; ++row)
        for (uint32 column = 0; valid && column < 32; ++column)
            if (((parsed[row] >> column) & 1u) != ((parsed[column] >> row) & 1u)) valid = false;
    if (valid) masks = parsed;
    else Log::Error("Invalid Layers section in GameSettings.ini; using the default collision matrix.");
}

//旧场景可能保存多个位，按这些层允许的目标取并集。
uint32 LayerSettings::GetCollisionMask(uint32 layers)
{
    uint32 result = 0;
    for (uint32 index = 0; index < 32; ++index)
        if ((layers & (1u << index)) != 0) result |= masks[index];
    return result;
}
