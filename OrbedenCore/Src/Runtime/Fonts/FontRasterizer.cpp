#include "Runtime/Fonts/FontRasterizer.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_MODULE_H

#include <msdfgen.h>

#include "Log/Log.h"

namespace
{
    //距离场参数：每 em 的像素数、取值范围、四周留白、轮廓边着色角度与随机种子。
    constexpr double DistanceFieldPixelsPerEm = 64.0;
    constexpr double DistanceFieldRange = 4.0;
    constexpr int32 DistanceFieldPadding = 6;
    constexpr double DistanceFieldEdgeAngle = 3.0;
    constexpr unsigned long long DistanceFieldSeed = 0;

    //Bitmap 投影像素字号：四舍五入后限于这个区间。
    constexpr int32 MinBitmapPixelSize = 1;
    constexpr int32 MaxBitmapPixelSize = 512;
    //字形位图四周各留一个 texel，线性过滤时边缘不会采到相邻字形。
    constexpr int32 BitmapPadding = 1;

    FT_Library library = nullptr;
    bool libraryReady = false;

    //一个字体对象对应的字体面；revision 变化时重新打开。
    struct FaceRecord
    {
        FT_Face face = nullptr;
        uint64 revision = 0;
    };

    std::unordered_map<int32, FaceRecord> faces;
    //字形位图的复用缓冲，所有权在 FontRasterizer。
    List<uint8> scratch;

    //按需初始化 FreeType；进程内只初始化一次。
    bool EnsureLibrary()
    {
        if (libraryReady) return true;
        if (FT_Init_FreeType(&library) != 0)
        {
            Log::Error("FontRasterizer: FT_Init_FreeType failed.");
            library = nullptr;
            return false;
        }
        libraryReady = true;
        return true;
    }

    //释放一个字体面并把元数据清空。
    void ReleaseFace(int32 objectId)
    {
        auto found = faces.find(objectId);
        if (found == faces.end()) return;
        if (found->second.face) FT_Done_Face(found->second.face);
        faces.erase(found);
    }

    //取得字体面；缺失或版本过期时重新打开。
    FT_Face GetFace(Font& font)
    {
        int32 objectId = font.GetObjectId();
        auto found = faces.find(objectId);
        if (found != faces.end() && found->second.revision == font.GetRevision())
            return found->second.face;

        //revision 变化说明字节换了，旧字体面必须先释放。
        if (found != faces.end()) ReleaseFace(objectId);
        if (!FontRasterizer::OpenFont(font)) return nullptr;

        found = faces.find(objectId);
        return found != faces.end() ? found->second.face : nullptr;
    }

    //把 FreeType 轮廓点转成 msdfgen 的点。
    msdfgen::Point2 ToPoint(const FT_Vector* vector)
    {
        return msdfgen::Point2(static_cast<double>(vector->x), static_cast<double>(vector->y));
    }

    //FT_Outline_Decompose 的回调上下文。
    struct OutlineContext
    {
        msdfgen::Shape* shape = nullptr;
        msdfgen::Contour* contour = nullptr;
        msdfgen::Point2 current;
        bool open = false;
    };

    int OutlineMoveTo(const FT_Vector* to, void* user)
    {
        OutlineContext& context = *static_cast<OutlineContext*>(user);
        context.current = ToPoint(to);
        context.contour = &context.shape->addContour();
        context.open = true;
        return 0;
    }

    int OutlineLineTo(const FT_Vector* to, void* user)
    {
        OutlineContext& context = *static_cast<OutlineContext*>(user);
        if (!context.open || !context.contour) return 0;
        msdfgen::Point2 point = ToPoint(to);
        context.contour->addEdge(msdfgen::EdgeHolder(context.current, point));
        context.current = point;
        return 0;
    }

    int OutlineConicTo(const FT_Vector* control, const FT_Vector* to, void* user)
    {
        OutlineContext& context = *static_cast<OutlineContext*>(user);
        if (!context.open || !context.contour) return 0;
        msdfgen::Point2 point = ToPoint(to);
        context.contour->addEdge(msdfgen::EdgeHolder(context.current, ToPoint(control), point));
        context.current = point;
        return 0;
    }

    int OutlineCubicTo(const FT_Vector* first, const FT_Vector* second, const FT_Vector* to, void* user)
    {
        OutlineContext& context = *static_cast<OutlineContext*>(user);
        if (!context.open || !context.contour) return 0;
        msdfgen::Point2 point = ToPoint(to);
        context.contour->addEdge(msdfgen::EdgeHolder(context.current, ToPoint(first), ToPoint(second), point));
        context.current = point;
        return 0;
    }

    //把字形轮廓转成 msdfgen 的形状；轮廓为空（例如空格）时返回假。
    bool BuildShape(FT_Face face, msdfgen::Shape& shape)
    {
        OutlineContext context;
        context.shape = &shape;
        FT_Outline_Funcs callbacks{};
        callbacks.move_to = &OutlineMoveTo;
        callbacks.line_to = &OutlineLineTo;
        callbacks.conic_to = &OutlineConicTo;
        callbacks.cubic_to = &OutlineCubicTo;
        callbacks.shift = 0;
        callbacks.delta = 0;

        if (FT_Outline_Decompose(&face->glyph->outline, &callbacks, &context) != 0) return false;
        return !shape.contours.empty();
    }

    //按距离场参数把形状渲染成 SDF 或 MSDF 字节。
    bool RasterizeDistanceField(Font& font, FT_Face face, FontRasterMode mode, FontGlyphBitmap& bitmap)
    {
        msdfgen::Shape shape;
        if (!BuildShape(face, shape)) return false;

        //规范化保证环绕方向与起点一致，边着色给出多通道所需的边颜色。
        shape.normalize();
        msdfgen::edgeColoringSimple(shape, DistanceFieldEdgeAngle, DistanceFieldSeed);

        uint32 unitsPerEm = font.unitsPerEm != 0 ? font.unitsPerEm : 1000;
        double scale = DistanceFieldPixelsPerEm / static_cast<double>(unitsPerEm);
        msdfgen::Shape::Bounds bounds = shape.getBounds();

        double width = (bounds.r - bounds.l) * scale;
        double height = (bounds.t - bounds.b) * scale;
        int32 outputWidth = static_cast<int32>(std::ceil(width)) + DistanceFieldPadding * 2;
        int32 outputHeight = static_cast<int32>(std::ceil(height)) + DistanceFieldPadding * 2;
        if (outputWidth <= 0 || outputHeight <= 0) return false;

        //留白换算回形状单位后写入平移，字形因此不会贴边。
        msdfgen::Vector2 translate(
            DistanceFieldPadding / scale - bounds.l,
            DistanceFieldPadding / scale - bounds.b);
        msdfgen::Vector2 scaleVector(scale, scale);
        msdfgen::Range range(DistanceFieldRange);

        if (mode == FontRasterMode::MSDF)
        {
            msdfgen::Bitmap<float, 3> image(outputWidth, outputHeight);
            msdfgen::generateMSDF(image, shape, range, scaleVector, translate);

            int32 rowStride = outputWidth * 3;
            scratch.assign(static_cast<usize>(rowStride) * outputHeight, 0);
            //按从上到下的行顺序写出字形像素
            for (int32 y = 0; y < outputHeight; ++y)
            {
                for (int32 x = 0; x < outputWidth; ++x)
                {
                    const float* pixel = image(x, outputHeight - 1 - y);
                    uint8* target = scratch.data() + static_cast<usize>(y) * rowStride + x * 3;
                    target[0] = msdfgen::pixelFloatToByte(pixel[0]);
                    target[1] = msdfgen::pixelFloatToByte(pixel[1]);
                    target[2] = msdfgen::pixelFloatToByte(pixel[2]);
                }
            }
            bitmap.channels = 3;
            bitmap.rowStride = rowStride;
        }
        else
        {
            msdfgen::Bitmap<float, 1> image(outputWidth, outputHeight);
            msdfgen::generateSDF(image, shape, range, scaleVector, translate);

            int32 rowStride = outputWidth;
            scratch.assign(static_cast<usize>(rowStride) * outputHeight, 0);
            //按从上到下的行顺序写出字形像素
            for (int32 y = 0; y < outputHeight; ++y)
            {
                for (int32 x = 0; x < outputWidth; ++x)
                    scratch[static_cast<usize>(y) * rowStride + x] = msdfgen::pixelFloatToByte(*image(x, outputHeight - 1 - y));
            }
            bitmap.channels = 1;
            bitmap.rowStride = rowStride;
        }

        //距离场按字形原点对齐：位图左下角相对原点的偏移是留白减去降部超出部分。
        bitmap.width = outputWidth;
        bitmap.height = outputHeight;
        bitmap.originX = -DistanceFieldPadding;
        bitmap.originY = static_cast<int32>(std::floor(bounds.b * scale)) - DistanceFieldPadding;
        bitmap.pixels = scratch.data();
        bitmap.byteCount = static_cast<int32>(scratch.size());
        return true;
    }

    //按投影像素字号渲染位图字形，四周补一圈留白。
    bool RasterizeBitmap(FT_Face face, uint32 glyphIndex, uint32 requestedPixelSize, FontGlyphBitmap& bitmap)
    {
        int32 pixelSize = static_cast<int32>(std::lround(static_cast<double>(requestedPixelSize)));
        pixelSize = std::clamp(pixelSize, MinBitmapPixelSize, MaxBitmapPixelSize);
        //hinting 让小幅文字更清晰；度量走的是另一条未 hint 的路径。
        if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize)) != 0) return false;
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER) != 0) return false;

        const FT_Bitmap& source = face->glyph->bitmap;
        if (source.width == 0 || source.rows == 0) return false;

        int32 width = static_cast<int32>(source.width) + BitmapPadding * 2;
        int32 height = static_cast<int32>(source.rows) + BitmapPadding * 2;
        int32 rowStride = width;
        scratch.assign(static_cast<usize>(rowStride) * height, 0);

        //pitch 为正表示自上而下、为负表示自下而上；两种都要处理。
        int32 pitch = source.pitch;
        int32 rowBytes = static_cast<int32>(source.width);
        for (int32 row = 0; row < static_cast<int32>(source.rows); ++row)
        {
            const uint8* sourceRow = pitch >= 0
                ? source.buffer + static_cast<usize>(row) * pitch
                : source.buffer + static_cast<usize>(source.rows - 1 - row) * (-pitch);
            uint8* targetRow = scratch.data() + static_cast<usize>(row + BitmapPadding) * rowStride + BitmapPadding;
            std::copy(sourceRow, sourceRow + rowBytes, targetRow);
        }

        bitmap.width = width;
        bitmap.height = height;
        bitmap.channels = 1;
        bitmap.rowStride = rowStride;
        //补偿补出来的那一圈留白，位图仍以字形原点对齐。
        bitmap.originX = face->glyph->bitmap_left - BitmapPadding;
        bitmap.originY = face->glyph->bitmap_top - static_cast<int32>(source.rows) - BitmapPadding;
        bitmap.pixels = scratch.data();
        bitmap.byteCount = static_cast<int32>(scratch.size());
        return true;
    }
}

namespace FontRasterizer
{
    bool OpenFont(Font& font)
    {
        if (font.sourceBytes.empty())
        {
            Log::Error("FontRasterizer: font has no source bytes.");
            return false;
        }
        if (!EnsureLibrary()) return false;

        int32 objectId = font.GetObjectId();
        auto found = faces.find(objectId);
        if (found != faces.end() && found->second.revision == font.GetRevision())
            return true;

        //revision 变化先释放旧字体面，避免两个字体面同时驻留。
        ReleaseFace(objectId);

        FT_Face face = nullptr;
        FT_Error error = FT_New_Memory_Face(library,
            reinterpret_cast<const FT_Byte*>(font.sourceBytes.data()),
            static_cast<FT_Long>(font.sourceBytes.size()),
            static_cast<FT_Long>(font.faceIndex), &face);
        if (error != 0 || !face)
        {
            Log::Error(error == FT_Err_Unknown_File_Format
                ? "FontRasterizer: unsupported font format."
                : "FontRasterizer: font face could not be opened (index out of range?).");
            return false;
        }

        //元数据在字体单位下解析一次，供检视面板与缺字行高使用。
        font.familyName = face->family_name ? face->family_name : "";
        font.styleName = face->style_name ? face->style_name : "";
        font.unitsPerEm = face->units_per_EM != 0 ? face->units_per_EM : 1000;
        font.ascender = static_cast<float32>(face->ascender);
        font.descender = static_cast<float32>(face->descender);
        font.lineHeight = static_cast<float32>(face->height);

        faces.emplace(objectId, FaceRecord{ face, font.GetRevision() });
        return true;
    }

    bool ValidateFontBytes(const List<uint8>& bytes, uint32 faceIndex)
    {
        if (bytes.empty() || !EnsureLibrary()) return false;

        FT_Face face = nullptr;
        FT_Error error = FT_New_Memory_Face(library,
            reinterpret_cast<const FT_Byte*>(bytes.data()),
            static_cast<FT_Long>(bytes.size()),
            static_cast<FT_Long>(faceIndex), &face);
        if (error != 0 || !face) return false;

        FT_Done_Face(face);
        return true;
    }

    bool GetGlyphMetrics(Font& font, uint32 scalar, FontGlyphMetrics& metrics)
    {
        FT_Face face = GetFace(font);
        if (!face) return false;

        uint32 glyphIndex = FT_Get_Char_Index(face, static_cast<FT_ULong>(scalar));
        metrics.glyphIndex = glyphIndex;
        //没有对应字形时如实返回 0，缺字由上层统一处理。
        if (glyphIndex == 0) return false;

        //未缩放加载：度量就是字体单位，不含 hinting 与设备像素的介入。
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) != 0) return false;

        const FT_Glyph_Metrics& source = face->glyph->metrics;
        metrics.advance = static_cast<float32>(source.horiAdvance);
        metrics.bearingX = static_cast<float32>(source.horiBearingX);
        metrics.bearingY = static_cast<float32>(source.horiBearingY);
        metrics.width = static_cast<float32>(source.width);
        metrics.height = static_cast<float32>(source.height);
        return true;
    }

    float32 GetKerning(Font& font, uint32 leftGlyph, uint32 rightGlyph)
    {
        FT_Face face = GetFace(font);
        if (!face || leftGlyph == 0 || rightGlyph == 0) return 0.0f;
        if (!FT_HAS_KERNING(face)) return 0.0f;

        FT_Vector kerning{};
        if (FT_Get_Kerning(face, leftGlyph, rightGlyph, FT_KERNING_UNSCALED, &kerning) != 0) return 0.0f;
        return static_cast<float32>(kerning.x);
    }

    bool RasterizeGlyph(Font& font, uint32 scalar, FontRasterMode mode, uint32 pixelSize, FontGlyphBitmap& bitmap)
    {
        bitmap = FontGlyphBitmap();
        FT_Face face = GetFace(font);
        if (!face) return false;

        uint32 glyphIndex = FT_Get_Char_Index(face, static_cast<FT_ULong>(scalar));
        //缺字不查备用字体、也不用 .notdef：统一交给上层画空心方框。
        if (glyphIndex == 0) return false;

        //Bitmap 由渲染加载自己按投影像号取字形；距离场按字体单位取未缩放的轮廓。
        if (mode == FontRasterMode::Bitmap) return RasterizeBitmap(face, glyphIndex, pixelSize, bitmap);
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0)
            return false;
        return RasterizeDistanceField(font, face, mode, bitmap);
    }

    void ReleaseFont(int32 objectId)
    {
        ReleaseFace(objectId);
    }

    void Shutdown()
    {
        for (auto& entry : faces)
        {
            if (entry.second.face) FT_Done_Face(entry.second.face);
        }
        faces.clear();
        scratch.clear();
        if (library)
        {
            FT_Done_FreeType(library);
            library = nullptr;
        }
        libraryReady = false;
    }
}
