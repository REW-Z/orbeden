#pragma once

#include "Runtime/Object/Font.h"

//预烘焙载荷采用小端存储；运行时沿用页游标进行动态补字。
namespace FontAtlasBaker
{
    //烘焙指定 UTF-8 字符，保存字形表、图集像素与装箱游标
    bool Bake(Font& font, const std::string& characters, uint32 bitmapPixelSize,
        List<uint8>& data, uint32& missingCount, std::string& error);

    //校验载荷布局及其与字体导入参数的一致性
    bool Validate(const List<uint8>& data, FontRasterMode mode, uint32 atlasSize, uint32 distanceFieldSize);
}
