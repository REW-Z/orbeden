#pragma once

//游戏项目的固定布局。
//
//只有内容根是"给用户用的"：内部目录结构完全自由，资源、场景、脚本放哪都行。
//内容根之外的一切位置都由引擎与构建系统强依赖，因此写死在这里，不再做成 .oeproj 属性：
//位置既然固定，可配置只会带来不一致。
namespace ProjectLayout
{
    //内容根：美术、音频、场景与代码脚本。Object 派生资源的 stringid 一律相对它解析。
    constexpr const char* ContentFolder = "Content";

    //SDK 快照：Core C# 运行库、绑定目标转发、OrbedenSdk.path。
    constexpr const char* LibraryFolder = "Lib";

    //构建产物根目录，其下全部由构建生成。
    constexpr const char* BuildFolder = "Build";

    //C# 开发程序集、中间产物与 PIE 影子副本。
    constexpr const char* ManagedFolder = "Build/Managed";

    //Player 的 NativeAOT 产物。
    constexpr const char* AotFolder = "Build/Aot";

    //原生模块的工程、导出层与产物。
    constexpr const char* NativeBuildFolder = "Build/Native";

    //原生模块的导出层源码，与工程文件同级放在项目根。
    constexpr const char* ModuleSourceFileName = "GameModule.cpp";

    //原生模块工程与产物的文件名后缀，前缀是项目名。
    constexpr const char* ModuleNameSuffix = "Native";
}
