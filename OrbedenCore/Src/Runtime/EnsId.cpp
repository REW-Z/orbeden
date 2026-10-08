#include "Runtime/EnsId.h"

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
