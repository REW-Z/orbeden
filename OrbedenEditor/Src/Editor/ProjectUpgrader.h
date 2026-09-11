#pragma once

#include <string>

//把旧版本 SDK 下建立的游戏项目升级到当前 OrbedenProjectVersion。
//
//内容根之外的一切都是引擎的地盘，升级时整块重建，不做增量修补：
//先按旧布局把内容归位到内容根，再把内容根之外清空，然后从模板重铺脚手架。
//唯一原样保留的是 Content/、.oeproj（取其 name 与 startupWorld）以及以点开头的条目。
namespace ProjectUpgrader
{
    struct UpgradeRequest
    {
        std::string projectRoot;
        std::string projectName;        //脚手架命名依据，取自 .oeproj 的文件基名
        std::string projectFilePath;    //<项目根>/<项目名>.oeproj
        std::string startupWorld;       //升级前的启动场景，重铺后若仍存在则恢复
        std::string runtimeDllPath;     //当前 SDK 的 OrbedenCore.CSharp.dll
        std::string templateRoot;       //Editor 分发目录中的 Templates/
    };

    //执行升级。全部步骤幂等；任一步失败立即中止且不写入新版本号，
    //这样下次打开会重新提示升级，不会留下"版本号已更新、脚手架还是旧的"这种无法自愈的状态。
    bool UpgradeProject(const UpgradeRequest& request, std::string& outError);
}
