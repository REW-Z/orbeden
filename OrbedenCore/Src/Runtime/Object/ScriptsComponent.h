#pragma once

#include "Runtime/EnsId.h"

#include <string>

//单个托管脚本槽位，保存 C# 类型和运行时实例状态
struct ScriptSlot
{
public:
    bool enabled = true;
    std::string assemblyName;
    std::string typeName;
    uint64 managedHandle = 0;
    bool started = false;
};

//脚本组件，一个 Ens 上保存多个 C# ScriptBehaviour
class ScriptsComponent : public Component
{
    OBJECT_TYPE_DECLARE(ScriptsComponent)

public:
    List<ScriptSlot> scripts;
};
