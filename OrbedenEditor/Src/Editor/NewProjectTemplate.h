#pragma once

#include <string>

//新项目模板，由 Editor 的 New Project 功能维护。
//模板内容以真实文件形式存放在 OrbedenEditor/Templates/ 下，随 Editor 一起分发。
namespace NewProjectTemplate
{
    //获取项目主程序集使用的固定 NativeAOT 导出薄层源码。
    const char* GetAotExportsText();

    //把模板目录复制到空项目目录，文本文件替换项目名占位符。
    bool GenerateProjectFiles(const std::string& projectRoot,
        const std::string& projectName,
        const std::string& templateDirectory,
        std::string& outError);
}
