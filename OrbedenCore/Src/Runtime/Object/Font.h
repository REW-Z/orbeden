#pragma once

#include "Runtime/Object/Object.h"

#include <string>

//字形光栅化模式。三种模式的 advance 完全相同，只有像素表示不同。
//Atlas 通道分别为 R8、R8、RGB8，都是线性数据，采样时不做 sRGB 解码。
enum class FontRasterMode : uint32
{
    Bitmap = 0,
    SDF = 1,
    MSDF = 2,
};

//字体资源：保存字体文件字节与字体面下标，元数据在导入时由字节解析。
//不保存 FT_Face、Atlas 或 GPU 句柄：字体面由 FontRasterizer 按需缓存，
//图集与纹理归托管侧的字体图集缓存。Player 只使用打包字节，不读系统字体。
class Font : public Object
{
    OBJECT_TYPE_DECLARE(Font)

private:
    //内容版本。重新导入成功后递增，字形与图集缓存据此失效。
    uint64 revision = 1;

public:
    //字体文件字节。支持 .ttf/.otf/.ttc，TTC 由 faceIndex 选择字体面。
    List<uint8> sourceBytes;
    //TTC 里的字体面下标；单面字体为 0。越界时导入失败。
    uint32 faceIndex = 0;

    //以下元数据在导入时从字节解析，供检视面板展示，不持久化。
    //升部、降部与行高使用未 hint 的字体单位；换算到 em 要除以 unitsPerEm。
    std::string familyName;
    std::string styleName;
    uint32 unitsPerEm = 0;
    float32 ascender = 0.0f;
    float32 descender = 0.0f;
    float32 lineHeight = 0.0f;

    //读取内容版本
    uint64 GetRevision() const;

    //推进内容版本；重新导入成功后由导入器调用
    ORBEDEN_BIND_IGNORE
    void BumpRevision();
};
