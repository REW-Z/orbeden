#pragma once

#include <string>

//新项目模板，由 Editor 的 New Project 功能维护。
namespace NewProjectTemplate
{
    //获取项目主程序集使用的固定 NativeAOT 导出薄层源码。
    const char* GetAotExportsText();

    //在空项目目录中生成默认资源、脚本和 World 文件。
    bool GenerateProjectFiles(const std::string& projectRoot,
        const std::string& projectName,
        std::string& outError);
}
