#pragma once

#include "Runtime/Object/Font.h"

//字形度量，使用未 hint 的字体单位。换算到像素要乘 fontSize / unitsPerEm。
struct FontGlyphMetrics
{
    //字形下标；0 表示该标量在字体里没有对应字形（缺字由上层统一处理）。
    uint32 glyphIndex = 0;
    //水平步进。
    float32 advance = 0.0f;
    //位图左下角相对字形原点的偏移。
    float32 bearingX = 0.0f;
    float32 bearingY = 0.0f;
    //位图尺寸。
    float32 width = 0.0f;
    float32 height = 0.0f;
};

//字形位图。pixels 指向 FontRasterizer 内部的复用缓冲，只在下一次调用之前有效；
//调用方要么立即复制，要么在下一次调用前用完。
struct FontGlyphBitmap
{
    //位图尺寸，单位像素。
    int32 width = 0;
    int32 height = 0;
    //通道数：Bitmap 与 SDF 为 1，MSDF 为 3。
    int32 channels = 0;
    //每行字节数。
    int32 rowStride = 0;
    //位图左下角相对字形原点的偏移，单位像素。
    int32 originX = 0;
    int32 originY = 0;
    //像素起始地址，所有权在 FontRasterizer。
    const uint8* pixels = nullptr;
    //像素字节数。
    int32 byteCount = 0;
};

//字体光栅化服务。只负责字体字节、字体面、字形度量与轮廓光栅化，
//不执行换行、不执行 Atlas 装箱——那些在托管侧。
namespace FontRasterizer
{
    //打开字体面并解析元数据。revision 变化时先释放旧字体面再重新打开。
    bool OpenFont(Font& font);

    //只校验一段字体字节能否按指定字体面打开，不驻留任何字体面。
    //读取 cooked 产物时先校验、再替换资源内容，避免半个字体写进资源。
    bool ValidateFontBytes(const List<uint8>& bytes, uint32 faceIndex);

    //查询标量对应的字形度量；字体里没有该字形时返回假，metrics.glyphIndex 为 0。
    bool GetGlyphMetrics(Font& font, uint32 scalar, FontGlyphMetrics& metrics);

    //查询两个字形之间的字距，单位是未 hint 的字体单位。
    float32 GetKerning(Font& font, uint32 leftGlyph, uint32 rightGlyph);

    //光栅化一个字形。Bitmap 用 pixelSize 指定投影像素字号，距离场模式忽略它。
    bool RasterizeGlyph(Font& font, uint32 scalar, FontRasterMode mode, uint32 pixelSize, FontGlyphBitmap& bitmap);

    //释放某个字体对象持有的字体面。
    void ReleaseFont(int32 objectId);

    //释放全部缓存；引擎关闭时调用。
    void Shutdown();
}
