#include "Runtime/GameSettingsFile.h"

#include "FileSystem/PathDefines.h"
#include "Runtime/Native/InteropText.h"

#include <filesystem>

//解析项目根下的配置文件路径
std::string GameSettingsFile::GetPath()
{
    const std::string& root = PathDefines::GetContentRoot();
    if (root.empty()) return std::string();

    return InteropText::PathToUtf8(InteropText::PathFromUtf8(root).parent_path() / FileName);
}
