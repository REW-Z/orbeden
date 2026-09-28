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
}
