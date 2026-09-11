#pragma once

#include "Defines/types.h"

#include <string>

//新项目模板，由 Editor 的 New Project 功能维护。
//模板内容以真实文件形式存放在 OrbedenEditor/Templates/ 下，随 Editor 一起分发。
//
//模板里没有占位符：复制到项目就能直接用，项目名由工程文件名与 .oeproj 文件基名决定。
//
//模板根下分为三部分：
//  Project/   项目脚手架，铺到项目根。
//  Examples/  示例内容，整体铺到 <项目根>/Content/Examples/，每次重铺都整目录重建。
//  Shared/    固定的桥接源码与共享属性表，发布到 SDK，不铺进项目。
//
//Project/ 中不得包含任何游戏内容：示例内容一旦与项目自身内容同名，
//会同时撞上 C# 的全限定类型名和 MetaGen 的类型字典。
namespace NewProjectTemplate
{
    //镜像结果统计，用于把项目里的改动写回模板时给出可核对的报告。
    struct MirrorReport
    {
    public:
        int32 added = 0;
        int32 updated = 0;
        int32 removed = 0;
    };

    //把一棵模板树复制到目标目录；文件名按规则改名，内容逐字节复制。
    //只覆盖不删除：目标中已存在但模板里没有的文件保持原样。
    bool CopyTemplateTree(const std::string& sourceDirectory,
        const std::string& targetDirectory,
        const std::string& projectName,
        std::string& outError);

    //把 sourceDirectory 镜像到 targetDirectory：复制有变化的文件，并删除目标里多余的。
    //内容相同的文件不重写，这样"什么都没改"的报告就是 0/0/0。
    //文本文件按行内容比较，忽略 CRLF 与 LF 的差异，免得一次 checkout 就把整棵树报成已改动。
    bool MirrorTree(const std::string& sourceDirectory,
        const std::string& targetDirectory,
        MirrorReport& outReport,
        std::string& outError);

    //在目录里找出现指定文本的文件（只查文本类文件），用于写回前拦截还没规范化的示例。
    bool CollectFilesContaining(const std::string& sourceDirectory,
        const std::string& token,
        List<std::string>& outFiles,
        std::string& outError);

    //把脚手架与示例铺到项目目录。
    bool GenerateProjectFiles(const std::string& projectRoot,
        const std::string& projectName,
        const std::string& templateRoot,
        std::string& outError);
}
