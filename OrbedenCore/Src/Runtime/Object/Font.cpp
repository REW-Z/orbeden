#include "Runtime/Object/Font.h"

OBJECT_TYPE_IMPLEMENT(Font, Object)

//读取内容版本
uint64 Font::GetRevision() const
{
    return revision;
}

//推进内容版本
void Font::BumpRevision()
{
    ++revision;
    if (revision == 0) revision = 1;
}
