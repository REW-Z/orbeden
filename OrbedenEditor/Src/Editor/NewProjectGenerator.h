#pragma once

#include <string>

//通用新建游戏项目生成器。
namespace NewProjectGenerator
{
    //创建新游戏项目目录、项目脚手架和 Examples 示例。
    //templateRoot 是模板根（含 Project/ 与 Examples/ 两个子目录）。
    bool CreateProject(const std::string& parentDirectory,
        const std::string& projectName,
        const std::string& runtimeDllPath,
        const std::string& templateRoot,
        std::string& outProjectRoot,
        std::string& outError);

    //同步游戏构建目标与 SDK 路径
    bool SyncGameBuildFiles(const std::string& scriptProjectPath, const std::string& runtimeDllPath, std::string& outError);

    //同步 Core C# 运行库与游戏构建配置
    bool SyncRuntimeCSharpDll(const std::string& scriptProjectPath, const std::string& runtimeDllPath, std::string& outError);

    //生成当前 SDK 的 VSCode C++ 配置
    bool WriteVSCodeConfiguration(const std::string& scriptProjectPath, const std::string& runtimeDllPath, std::string& outError);

    //修复并迁移脚本工程的 MSBuild 配置。
    bool RepairScriptProjectBuildProps(const std::string& scriptProjectPath, std::string& outError);

}
