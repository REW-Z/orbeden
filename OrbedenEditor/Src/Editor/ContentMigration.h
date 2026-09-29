#pragma once

#include "Defines/types.h"
#include "Runtime/EngineTypes.h"

#include <string>

//内容根内的版本迁移。
//与 ProjectUpgrader 的分工互斥：升级器只碰内容根之外，这里只读改内容根之内的场景与预制体，
//用于把字段改名、Shader 接口这类「格式变更靠改文件」的项目内容一次性升到当前版本。
namespace ContentMigration
{
    struct MigrationReport
    {
    public:
        //被改写的场景或预制体数量
        int32 rewrittenFiles = 0;
        //被改写的字段条数
        int32 rewrittenFields = 0;
        //被删除的旧位置文件数量
        int32 removedFiles = 0;
        //被删除的旧位置文件（内容根相对 Key）
        List<std::string> removedKeys;
        //仍需作者迁移的自定义 Shader（内容根相对 Key）
        List<std::string> pendingShaderKeys;
    };

    //按起始版本迁移内容根。起始版本已经达到当前版本时不做事，返回 true。
    bool MigrateForVersion(const std::string& contentRoot, uint32 fromVersion, MigrationReport& outReport, std::string& outError);

    //铺入 v29 的大气 Shader 与 include，并把仍等于 v28 基线的内置 Shader 换成接入空气透视的版本。
    //升级器保留 Content，内置内容不会随模板重铺，只能在这里显式发布。报告与 MigrateForVersion 累加。
    bool MigrateAtmosphereAssets(const std::string& contentRoot, const std::string& templateRoot,
        MigrationReport& outReport, std::string& outError);

    //铺入 v30 的浓雾 include，并把仍等于 v29 基线的大气 include 与两处天空绘制换成接入浓雾的版本。
    //这一级里 skybox 还在项目级 Shaders 下，v31 起它才与其余文件一样来自 Builtin。
    bool MigrateDenseFogAssets(const std::string& contentRoot, const std::string& templateRoot,
        MigrationReport& outReport, std::string& outError);

    //v31：阴影与天空两个引擎 Shader 从项目骨架转正到 Builtin。
    //铺入 Builtin/Shaders 的模板版本，并删除项目级 Shaders 下的旧位置文件及其伴生 .resinfo。
    //旧位置一律删除：引擎按文件名在内容根内查找，同名两份会打告警并取字典序靠前的一份；
    //作者写在旧文件里的定制会一并丢弃。这两个 Shader 不能改名，要定制只能直接改这份同名文件。
    bool MigrateBuiltinEngineShaders(const std::string& contentRoot, const std::string& templateRoot,
        MigrationReport& outReport, std::string& outError);
}
