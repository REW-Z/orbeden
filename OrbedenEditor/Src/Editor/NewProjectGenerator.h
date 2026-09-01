#pragma once

#include <string>

//通用新建游戏项目生成器。
namespace NewProjectGenerator
{
    //创建新游戏项目目录、默认 World 和 C# 脚本工程。
    bool CreateProject(const std::string& parentDirectory,
        const std::string& projectName,
        const std::string& runtimeDllPath,
        std::string& outProjectRoot,
        std::string& outError);

    //修复并迁移脚本工程的 MSBuild 配置。
    bool RepairScriptProjectBuildProps(const std::string& scriptProjectPath, std::string& outError);
}
