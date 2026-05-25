#include "Runtime/ObjectSystem.h"

#include "Runtime/Ens.h"

//获取对象系统数据
ObjectSystem& GetObjectSystem()
{
    static ObjectSystem system;
    return system;
}

//生成组件映射Key
uint64 GetComponentKey(EnsId ens, Type* type)
{
    return (static_cast<uint64>(ens.id) << 32) | static_cast<uint64>(type->GetId());
}

//判断Ens句柄是否有效
bool IsEnsAlive(EnsId ens)
{
    ObjectSystem& system = GetObjectSystem();
    if (ens.IsNull()) return false;
    if (ens.id >= system.enses.size()) return false;

    const EnsRecord& record = system.enses[ens.id];
    return record.alive && record.version == ens.version;
}

//获取基础组件
EnsComponent* GetBasicComponent(EnsId ens)
{
    ObjectSystem& system = GetObjectSystem();
    if (!IsEnsAlive(ens)) return nullptr;

    return system.enses[ens.id].basic;
}

//注册组件
void RegisterComponent(EnsId ens, Component* component)
{
    if (!component) return;

    ObjectSystem& system = GetObjectSystem();
    system.componentByEnsAndType[GetComponentKey(ens, component->GetType())] = component;
}

//注销组件
void UnregisterComponent(EnsId ens, Type* type)
{
    ObjectSystem& system = GetObjectSystem();
    system.componentByEnsAndType.erase(GetComponentKey(ens, type));
}
