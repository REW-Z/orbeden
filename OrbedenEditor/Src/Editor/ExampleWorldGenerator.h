#pragma once

#include <string>

class Application;

//示例项目 World 生成器，由 Editor 维护示例场景源数据。
namespace ExampleWorldGenerator
{
    //判断项目是否使用内置示例 World 生成器
    bool IsExampleProject(const std::string& projectName);

    //生成示例 World 文件
    bool GenerateWorldFile(const std::string& projectRoot, const std::string& startupWorld);

    //补齐示例场景的运行时渲染环境
    void ApplyRuntimeEnvironment(Application& app);
}
