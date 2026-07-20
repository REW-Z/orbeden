#pragma once

#include <string>

//新项目模板，由 Editor 的 New Project 功能维护。
namespace NewProjectTemplate
{
    //在空项目目录中生成默认资源、脚本和 World 文件。
    bool GenerateProjectFiles(const std::string& projectRoot,
        const std::string& projectName,
        std::string& outError);
}
