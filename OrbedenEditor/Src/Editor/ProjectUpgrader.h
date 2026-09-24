#pragma once

#include <string>

//把旧版本 SDK 下建立的游戏项目升级到当前 OrbedenProjectVersion。
//
//游戏资产只存在于内容根内，内容根之外的一切都是引擎的地盘：升级不做增量修补，
//直接清空内容根之外，再从模板重铺脚手架。Content/ 原封不动，升级既不读也不写它。
//唯一原样保留的是 Content/、.oeproj（只改根属性）以及以点开头的条目。
//
//内容根之外的任何东西都会被删除：缓存、构建产物，以及放在那里的散装资源。
namespace ProjectUpgrader
{
    struct UpgradeRequest
    {
        std::string projectRoot;
        std::string projectName;        //脚手架命名依据，取自 .oeproj 的文件基名
        std::string projectFilePath;    //<项目根>/<项目名>.oeproj
        std::string runtimeDllPath;     //当前 SDK 的 OrbedenCore.CSharp.dll
        std::string templateRoot;       //Editor 分发目录中的 Templates/
    };

    //执行升级。全部步骤幂等；任一步失败立即中止且不写入新版本号，
    //这样下次打开会重新提示升级，不会留下"版本号已更新、脚手架还是旧的"这种无法自愈的状态。
    bool UpgradeProject(const UpgradeRequest& request, std::string& outError);
}
