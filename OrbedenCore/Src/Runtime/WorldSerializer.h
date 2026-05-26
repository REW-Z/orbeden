#pragma once

#include <string>

#include "Runtime/World.h"

class WorldSerializer
{
public:
    //从 XML 文件反序列化 World
    static bool LoadXml(World& world, const std::string& path);

    //将 World 序列化到 XML 文件
    static bool SaveXml(const World& world, const std::string& path);
};
