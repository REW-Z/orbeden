#pragma once

#include <string>

#include "Runtime/World.h"

class WorldSerializer
{
public:
    //捕获组件全部持久化字段及其稳定身份。
    static std::string CaptureComponent(Component* component);

    //从完整快照恢复组件及其挂载位置。
    static Component* RestoreComponent(Ens& ens, const std::string& snapshot, int32 index);

    //获取最近一次 World 加载遇到的未注册组件类型。
    static const std::string& GetLastUnregisteredComponentType();

    //从 XML 文件反序列化 World
    static bool LoadXml(World& world, const std::string& path);

    //将 World 序列化到 XML 文件
    static bool SaveXml(const World& world, const std::string& path);
};
