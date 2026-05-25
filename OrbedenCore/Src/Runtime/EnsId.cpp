#include "Runtime/EnsId.h"

#include "Runtime/Ens.h"

OBJECT_TYPE_IMPLEMENT(Component, Object)

//判断句柄是否为空
bool EnsId::IsNull() const
{
    return id == InvalidId;
}

bool EnsId::operator==(const EnsId& other) const
{
    return id == other.id && version == other.version;
}

bool EnsId::operator!=(const EnsId& other) const
{
    return !(*this == other);
}

//获取所属Ens
Ens Component::GetEns() const
{
    return Ens::FromEns(GetWorld(), owner);
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
