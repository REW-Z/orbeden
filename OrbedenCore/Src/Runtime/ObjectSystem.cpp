#include "Runtime/ObjectSystem.h"

#include "Runtime/Actor.h"

//获取对象系统数据
ObjectSystem& GetObjectSystem()
{
    static ObjectSystem system;
    return system;
}

//生成组件映射Key
uint64 GetComponentKey(Ens ens, Type* type)
{
    return (static_cast<uint64>(ens.id) << 32) | static_cast<uint64>(type->GetId());
}

//判断Actor句柄是否有效
bool IsEnsAlive(Ens ens)
{
    ObjectSystem& system = GetObjectSystem();
    if (ens.IsNull()) return false;
    if (ens.id >= system.actors.size()) return false;

    const ActorRecord& record = system.actors[ens.id];
    return record.alive && record.version == ens.version;
}

//获取基础组件
ActorComponent* GetBasicComponent(Ens ens)
{
    ObjectSystem& system = GetObjectSystem();
    if (!IsEnsAlive(ens)) return nullptr;

    return system.actors[ens.id].basic;
}

//注册组件
void RegisterComponent(Ens ens, Component* component)
{
    if (!component) return;

    ObjectSystem& system = GetObjectSystem();
    system.componentByActorAndType[GetComponentKey(ens, component->GetType())] = component;
}

//注销组件
void UnregisterComponent(Ens ens, Type* type)
{
    ObjectSystem& system = GetObjectSystem();
    system.componentByActorAndType.erase(GetComponentKey(ens, type));
}
