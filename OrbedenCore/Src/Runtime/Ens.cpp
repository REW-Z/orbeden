#include "Runtime/Ens.h"

#include "Runtime/Actor.h"

OBJECT_TYPE_IMPLEMENT(Component, Object)

//判断句柄是否为空
bool Ens::IsNull() const
{
    return id == InvalidId;
}

bool Ens::operator==(const Ens& other) const
{
    return id == other.id && version == other.version;
}

bool Ens::operator!=(const Ens& other) const
{
    return !(*this == other);
}

//获取所属Actor
Actor Component::GetActor() const
{
    return Actor::FromEns(owner);
}

//获取所属句柄
Ens Component::GetOwner() const
{
    return owner;
}

//设置所属句柄
void Component::SetOwner(Ens value)
{
    owner = value;
}
