#include "Runtime/Object/Component.h"

#include "Runtime/Ens.h"
#include "Runtime/World.h"

OBJECT_TYPE_IMPLEMENT(Component, Object)

//获取所属Ens
Ens* Component::GetEns() const
{
    World* world = GetWorld();
    return world ? world->GetEns(owner) : nullptr;
}

//获取所属句柄
EnsId Component::GetEnsId() const
{
    return owner;
}

//设置所属句柄
void Component::SetEnsId(EnsId value)
{
    owner = value;
}
