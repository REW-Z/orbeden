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
    //skybox 来自 Project/Content，其余来自 Builtin，两者的模板来源目录不同。
    bool MigrateDenseFogAssets(const std::string& contentRoot, const std::string& templateRoot,
        MigrationReport& outReport, std::string& outError);
}
