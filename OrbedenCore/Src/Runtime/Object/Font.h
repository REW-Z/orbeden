#pragma once

#include "Runtime/Object/Object.h"

#include <string>

//字形光栅化模式。三种模式的 advance 完全相同，只有像素表示不同。
//Atlas 通道分别为 R8、R8、RGB8，都是线性数据，采样时不做 sRGB 解码。
//托管侧字体图集缓存按值使用它，属于跨语言合同。
ORBEDEN_BIND_EXPORT
enum class FontRasterMode : uint32
{
    Bitmap = 0,
    SDF = 1,
    MSDF = 2,
};

//字体资源：保存字体文件字节与字体面下标，元数据在导入时由字节解析。
//保存预烘焙图集像素与字形表；字体面及 GPU 纹理由运行时按需建立。
//Player 使用打包的图集与字体字节，不读系统字体。
class Font : public Object
{
    OBJECT_TYPE_DECLARE(Font)

private:
    //内容版本。重新导入成功后递增，字形与图集缓存据此失效。
    uint64 revision = 1;
    //预烘焙载荷，布局由 FontAtlasBaker 定义；不保存运行时纹理对象。
    List<uint8> prebakedAtlas;

public:
    //字体文件字节。支持 .ttf/.otf/.ttc，TTC 由 faceIndex 选择字体面。
    List<uint8> sourceBytes;
    //TTC 里的字体面下标；单面字体为 0。越界时导入失败。
    uint32 faceIndex = 0;

    //导入时选定的字形表示；UI 组件使用该设置，不单独选择模式。
    FontRasterMode rasterMode = FontRasterMode::Bitmap;
    //预烘焙和动态补字图集页边长，支持 256 到 4096 的二次幂。
    uint32 atlasSize = 1024;
    //距离场每 em 的像素数，支持 16 到 256。
    uint32 distanceFieldSize = 64;
    //距离场取值范围，单位 texel，支持 1 到 32。
    float32 distanceFieldRange = 4.0f;

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

    //读取预烘焙图集载荷
    ORBEDEN_BIND_IGNORE
    const List<uint8>& GetPrebakedAtlas() const;

    //替换预烘焙图集载荷
    ORBEDEN_BIND_IGNORE
    void SetPrebakedAtlas(List<uint8> data);

    //推进内容版本；重新导入成功后由导入器调用
    ORBEDEN_BIND_IGNORE
    void BumpRevision();

    //校验字体导入参数
    ORBEDEN_BIND_IGNORE
    static bool ValidateImportSettings(FontRasterMode mode, uint32 atlasSize, uint32 distanceFieldSize, float32 distanceFieldRange);
};
