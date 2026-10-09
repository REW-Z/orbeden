#include "Runtime/Fonts/FontAtlasBaker.h"
#include "Runtime/Fonts/FontRasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <unordered_map>
#include <utility>

namespace
{
    constexpr uint32 PayloadVersion = 1;
    constexpr usize PixelByteLimit = 64u * 1024u * 1024u;
    constexpr uint32 GlyphLimit = 262144;

    struct Page
    {
        uint32 width = 0, height = 0, channels = 0;
        uint32 cursorX = 0, cursorY = 0, rowHeight = 0;
        List<uint8> pixels;

        //分配字形区域并推进逐行装箱游标
        bool Allocate(uint32 glyphWidth, uint32 glyphHeight, uint32& x, uint32& y)
        {
            uint32 nextX = cursorX, nextY = cursorY, nextHeight = rowHeight;
            if (glyphWidth > width || glyphHeight > height) return false;
            if (nextX + glyphWidth > width) { nextX = 0; nextY += nextHeight; nextHeight = 0; }
            if (nextY + glyphHeight > height) return false;
            x = nextX; y = nextY;
            cursorX = nextX + glyphWidth; cursorY = nextY; rowHeight = std::max(nextHeight, glyphHeight);
            return true;
        }
    };

    struct Glyph
    {
        uint32 scalar = 0;
        FontGlyphMetrics metrics;
        uint32 page = std::numeric_limits<uint32>::max();
        uint32 x = 0, y = 0, width = 0, height = 0;
        int32 originX = 0, originY = 0;
    };

    //写入小端整数
    void WriteUInt(List<uint8>& data, uint32 value)
    {
        for (uint32 shift = 0; shift < 32; shift += 8) data.push_back(static_cast<uint8>(value >> shift));
    }

    //写入小端浮点数
    void WriteFloat(List<uint8>& data, float32 value)
    {
        uint32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        WriteUInt(data, bits);
    }

    struct Reader
    {
        const List<uint8>& data;
        usize cursor = 0;

        //读取小端整数
        bool Read(uint32& value)
        {
            if (data.size() - cursor < 4) return false;
            value = 0;
            for (uint32 shift = 0; shift < 32; shift += 8) value |= static_cast<uint32>(data[cursor++]) << shift;
            return true;
        }

        //读取有限浮点数
        bool ReadFloat()
        {
            uint32 bits = 0;
            if (!Read(bits)) return false;
            float32 value = 0;
            std::memcpy(&value, &bits, sizeof(value));
            return std::isfinite(value);
        }
    };

    //解析 UTF-8 并收集去重后的可显示字符
    bool ReadCharacters(const std::string& text, std::set<uint32>& scalars)
    {
        for (usize index = 0; index < text.size();)
        {
            uint32 first = static_cast<uint8>(text[index++]);
            uint32 scalar = first, count = 0, minimum = 0;
            if (first >= 0xc2 && first <= 0xdf) { scalar &= 0x1f; count = 1; minimum = 0x80; }
            else if (first >= 0xe0 && first <= 0xef) { scalar &= 0x0f; count = 2; minimum = 0x800; }
            else if (first >= 0xf0 && first <= 0xf4) { scalar &= 0x07; count = 3; minimum = 0x10000; }
            else if (first >= 0x80) return false;
            if (index + count > text.size()) return false;
            for (uint32 continuation = 0; continuation < count; ++continuation)
            {
                uint32 next = static_cast<uint8>(text[index++]);
                if ((next & 0xc0) != 0x80) return false;
                scalar = (scalar << 6) | (next & 0x3f);
            }
            if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
            if (scalar >= 0x20 && !(scalar >= 0x7f && scalar <= 0x9f)) scalars.insert(scalar);
            if (scalars.size() > GlyphLimit) return false;
        }
        return true;
    }
}

//烘焙字符并序列化图集页及字形表
bool FontAtlasBaker::Bake(Font& font, const std::string& characters, uint32 bitmapPixelSize,
    List<uint8>& data, uint32& missingCount, std::string& error)
{
    data.clear(); missingCount = 0; error.clear();
    if (bitmapPixelSize < 1 || bitmapPixelSize > 512
        || !Font::ValidateImportSettings(font.rasterMode, font.atlasSize, font.distanceFieldSize, font.distanceFieldRange))
    { error = "Invalid prebake pixel size or font atlas settings."; return false; }
    std::set<uint32> scalars;
    if (!ReadCharacters(characters, scalars))
    { error = "Invalid UTF-8 prebake characters or character count exceeds the limit."; return false; }
    if (scalars.empty()) return true;

    List<Page> pages;
    List<Glyph> glyphs;
    std::unordered_map<uint32, usize> glyphIndices;
    usize pixelBytes = 0;
    uint32 channels = font.rasterMode == FontRasterMode::MSDF ? 3 : 1;
    uint32 pixelSize = font.rasterMode == FontRasterMode::Bitmap ? bitmapPixelSize : font.distanceFieldSize;

    //光栅化并装箱去重字形
    for (uint32 scalar : scalars)
    {
        Glyph glyph;
        glyph.scalar = scalar;
        if (!FontRasterizer::GetGlyphMetrics(font, scalar, glyph.metrics)) { ++missingCount; continue; }
        auto previous = glyphIndices.find(glyph.metrics.glyphIndex);
        if (previous != glyphIndices.end())
        {
            glyph = glyphs[previous->second]; glyph.scalar = scalar;
            glyphs.push_back(glyph);
            continue;
        }
        if (glyph.metrics.width > 0 && glyph.metrics.height > 0)
        {
            FontGlyphBitmap bitmap;
            if (!FontRasterizer::RasterizeGlyph(font, scalar, font.rasterMode, pixelSize, bitmap))
            { error = "Failed to rasterize prebake character: " + std::to_string(scalar); return false; }
            if (bitmap.width > 0 && bitmap.height > 0)
            {
                glyph.width = static_cast<uint32>(bitmap.width); glyph.height = static_cast<uint32>(bitmap.height);
                glyph.originX = bitmap.originX; glyph.originY = bitmap.originY;
                if (glyph.width > 4096 || glyph.height > 4096 || bitmap.channels != static_cast<int32>(channels)
                    || bitmap.rowStride < static_cast<int64>(glyph.width) * channels || !bitmap.pixels
                    || static_cast<int64>(bitmap.rowStride) * (glyph.height - 1) + static_cast<int64>(glyph.width) * channels > bitmap.byteCount)
                { error = "Invalid or oversized prebaked glyph bitmap: " + std::to_string(scalar); return false; }
                for (uint32 index = 0; index < pages.size(); ++index)
                {
                    if (pages[index].width == font.atlasSize && pages[index].Allocate(glyph.width, glyph.height, glyph.x, glyph.y))
                    { glyph.page = index; break; }
                }
                if (glyph.page == std::numeric_limits<uint32>::max())
                {
                    uint32 edge = font.atlasSize;
                    while (edge < glyph.width || edge < glyph.height) edge *= 2;
                    if (edge > 4096 || pixelBytes + static_cast<usize>(edge) * edge * channels > PixelByteLimit)
                    { error = "Prebaked atlas exceeds the 4096 pixel page or 64 MiB pixel limit."; return false; }
                    Page page;
                    page.width = page.height = edge; page.channels = channels;
                    page.pixels.resize(static_cast<usize>(edge) * edge * channels, 0);
                    page.Allocate(glyph.width, glyph.height, glyph.x, glyph.y);
                    pixelBytes += page.pixels.size();
                    glyph.page = static_cast<uint32>(pages.size());
                    pages.push_back(std::move(page));
                }
                Page& page = pages[glyph.page];
                for (uint32 row = 0; row < glyph.height; ++row)
                    std::memcpy(page.pixels.data() + (static_cast<usize>(glyph.y + row) * page.width + glyph.x) * channels,
                        bitmap.pixels + static_cast<usize>(row) * bitmap.rowStride, static_cast<usize>(glyph.width) * channels);
            }
        }
        glyphIndices.emplace(glyph.metrics.glyphIndex, glyphs.size());
        glyphs.push_back(glyph);
    }

    //写入载荷头与图集页
    WriteUInt(data, PayloadVersion); WriteUInt(data, static_cast<uint32>(font.rasterMode));
    WriteUInt(data, pixelSize); WriteUInt(data, font.atlasSize); WriteUInt(data, font.unitsPerEm);
    WriteUInt(data, static_cast<uint32>(pages.size())); WriteUInt(data, static_cast<uint32>(glyphs.size()));
    for (const Page& page : pages)
    {
        WriteUInt(data, page.width); WriteUInt(data, page.height); WriteUInt(data, page.channels);
        WriteUInt(data, page.cursorX); WriteUInt(data, page.cursorY); WriteUInt(data, page.rowHeight);
        WriteUInt(data, static_cast<uint32>(page.pixels.size()));
        data.insert(data.end(), page.pixels.begin(), page.pixels.end());
    }

    //写入字符映射与字形度量
    for (const Glyph& glyph : glyphs)
    {
        WriteUInt(data, glyph.scalar); WriteUInt(data, glyph.metrics.glyphIndex); WriteUInt(data, glyph.page);
        WriteUInt(data, glyph.x); WriteUInt(data, glyph.y); WriteUInt(data, glyph.width); WriteUInt(data, glyph.height);
        WriteUInt(data, static_cast<uint32>(glyph.originX)); WriteUInt(data, static_cast<uint32>(glyph.originY));
        WriteFloat(data, glyph.metrics.advance); WriteFloat(data, glyph.metrics.bearingX); WriteFloat(data, glyph.metrics.bearingY);
        WriteFloat(data, glyph.metrics.width); WriteFloat(data, glyph.metrics.height);
    }
    return true;
}

//校验预烘焙图集载荷
bool FontAtlasBaker::Validate(const List<uint8>& data, FontRasterMode mode, uint32 atlasSize, uint32 distanceFieldSize)
{
    if (data.empty()) return true;
    if (data.size() > PixelByteLimit + static_cast<usize>(GlyphLimit) * 56u + 32768u) return false;
    Reader reader{data};
    uint32 version = 0, storedMode = 0, pixelSize = 0, storedAtlasSize = 0, unitsPerEm = 0, pageCount = 0, glyphCount = 0;
    if (!reader.Read(version) || version != PayloadVersion || !reader.Read(storedMode) || storedMode != static_cast<uint32>(mode)
        || !reader.Read(pixelSize) || !reader.Read(storedAtlasSize) || storedAtlasSize != atlasSize
        || !reader.Read(unitsPerEm) || unitsPerEm == 0 || !reader.Read(pageCount) || pageCount > 1024
        || !reader.Read(glyphCount) || glyphCount > GlyphLimit) return false;
    if (mode == FontRasterMode::Bitmap ? (pixelSize < 1 || pixelSize > 512) : pixelSize != distanceFieldSize) return false;

    //校验图集尺寸、装箱游标及像素范围
    List<Page> pages(pageCount);
    usize pixelBytes = 0;
    for (Page& page : pages)
    {
        uint32 count = 0;
        if (!reader.Read(page.width) || !reader.Read(page.height) || !reader.Read(page.channels)
            || !reader.Read(page.cursorX) || !reader.Read(page.cursorY) || !reader.Read(page.rowHeight) || !reader.Read(count)) return false;
        if (page.width < atlasSize || page.width > 4096 || (page.width & (page.width - 1)) != 0 || page.height != page.width
            || page.channels != (mode == FontRasterMode::MSDF ? 3u : 1u) || page.cursorX > page.width
            || page.cursorY > page.height || page.rowHeight > page.height - page.cursorY
            || count != static_cast<usize>(page.width) * page.height * page.channels || count > data.size() - reader.cursor) return false;
        pixelBytes += count;
        if (pixelBytes > PixelByteLimit) return false;
        reader.cursor += count;
    }

    //校验字符、字形区域与度量
    std::set<uint32> scalars;
    for (uint32 index = 0; index < glyphCount; ++index)
    {
        uint32 scalar = 0, glyphIndex = 0, pageIndex = 0, x = 0, y = 0, width = 0, height = 0, origin = 0;
        if (!reader.Read(scalar) || !reader.Read(glyphIndex) || !reader.Read(pageIndex) || !reader.Read(x) || !reader.Read(y)
            || !reader.Read(width) || !reader.Read(height) || !reader.Read(origin) || !reader.Read(origin)) return false;
        if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff) || glyphIndex == 0 || !scalars.insert(scalar).second) return false;
        if (pageIndex == std::numeric_limits<uint32>::max())
        { if (width != 0 || height != 0 || x != 0 || y != 0) return false; }
        else if (pageIndex >= pages.size() || width == 0 || height == 0 || x > pages[pageIndex].width
            || width > pages[pageIndex].width - x || y > pages[pageIndex].height || height > pages[pageIndex].height - y) return false;
        for (uint32 field = 0; field < 5; ++field) if (!reader.ReadFloat()) return false;
    }
    return reader.cursor == data.size();
}
