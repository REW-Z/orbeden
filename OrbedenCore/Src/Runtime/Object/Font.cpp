#include "Runtime/Object/Font.h"

#include <cmath>
#include <utility>

OBJECT_TYPE_IMPLEMENT(Font, Object)

//读取内容版本
uint64 Font::GetRevision() const
{
    return revision;
}

//读取预烘焙图集载荷
const List<uint8>& Font::GetPrebakedAtlas() const
{
    return prebakedAtlas;
}

//替换预烘焙图集载荷
void Font::SetPrebakedAtlas(List<uint8> data)
{
    prebakedAtlas = std::move(data);
}

//推进内容版本
void Font::BumpRevision()
{
    ++revision;
    if (revision == 0) revision = 1;
}

//校验字体导入参数
bool Font::ValidateImportSettings(FontRasterMode mode, uint32 atlasSize, uint32 distanceFieldSize, float32 distanceFieldRange)
{
    return mode <= FontRasterMode::MSDF
        && atlasSize >= 256 && atlasSize <= 4096 && (atlasSize & (atlasSize - 1)) == 0
        && distanceFieldSize >= 16 && distanceFieldSize <= 256
        && std::isfinite(distanceFieldRange) && distanceFieldRange >= 1.0f && distanceFieldRange <= 32.0f;
}
