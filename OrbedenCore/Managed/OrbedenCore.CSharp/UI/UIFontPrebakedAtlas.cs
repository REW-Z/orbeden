using System;
using System.Collections.Generic;
using System.IO;

namespace Orbeden;

/// <summary>字体导入产物的字形表和图集像素；纹理页按使用情况加载。</summary>
internal sealed class UIFontPrebakedAtlas
{
    internal readonly record struct Page(int Width, int Height, int Channels, UIFontAtlasCursor Cursor, int PixelOffset, int ByteCount);
    internal readonly record struct Glyph(uint Scalar, uint GlyphIndex, int PageIndex, int X, int Y,
        int Width, int Height, int OriginX, int OriginY, float Advance, float BearingX, float BearingY, float MetricWidth, float MetricHeight);

    internal const int MaximumPayloadBytes = 64 * 1024 * 1024 + 262144 * 56 + 32768;
    internal readonly byte[] data;
    internal readonly FontRasterMode rasterMode;
    internal readonly int pixelSize;
    internal readonly int unitsPerEm;
    internal readonly Page[] pages;
    internal readonly UIFontAtlasPage?[] runtimePages;
    internal readonly Dictionary<uint, Glyph> glyphs;

    //保存图集载荷及其索引
    private UIFontPrebakedAtlas(byte[] data, FontRasterMode mode, int pixelSize, int unitsPerEm, Page[] pages, Dictionary<uint, Glyph> glyphs)
    {
        this.data = data;
        rasterMode = mode;
        this.pixelSize = pixelSize;
        this.unitsPerEm = unitsPerEm;
        this.pages = pages;
        runtimePages = new UIFontAtlasPage?[pages.Length];
        this.glyphs = glyphs;
    }

    /// <summary>解析小端预烘焙载荷，拒绝截断或越界的数据。</summary>
    internal static UIFontPrebakedAtlas Read(byte[] data)
    {
        if (data.Length > MaximumPayloadBytes) throw new InvalidDataException("Prebaked font atlas exceeds the byte limit.");
        using MemoryStream stream = new(data, writable: false);
        using BinaryReader reader = new(stream);
        if (reader.ReadUInt32() != 1) throw new InvalidDataException("Unsupported prebaked font atlas version.");
        FontRasterMode mode = (FontRasterMode)reader.ReadUInt32();
        int pixelSize = reader.ReadInt32();
        int atlasSize = reader.ReadInt32();
        int unitsPerEm = reader.ReadInt32();
        int pageCount = reader.ReadInt32();
        int glyphCount = reader.ReadInt32();
        if (mode is not (FontRasterMode.Bitmap or FontRasterMode.SDF or FontRasterMode.MSDF)
            || pixelSize is < 1 or > 512 || atlasSize is < 256 or > 4096 || (atlasSize & (atlasSize - 1)) != 0
            || unitsPerEm <= 0 || pageCount is < 0 or > 1024 || glyphCount is < 0 or > 262144)
            throw new InvalidDataException("Invalid prebaked font atlas header.");

        //读取图集页与像素位置
        Page[] pages = new Page[pageCount];
        long pixelBytes = 0;
        for (int index = 0; index < pageCount; ++index)
        {
            int width = reader.ReadInt32(), height = reader.ReadInt32(), channels = reader.ReadInt32();
            UIFontAtlasCursor cursor = new() { cursorX = reader.ReadInt32(), cursorY = reader.ReadInt32(), rowHeight = reader.ReadInt32() };
            int count = reader.ReadInt32();
            if (width < atlasSize || width > 4096 || (width & (width - 1)) != 0 || height != width
                || channels != (mode == FontRasterMode.MSDF ? 3 : 1) || cursor.cursorX < 0 || cursor.cursorX > width
                || cursor.cursorY < 0 || cursor.cursorY > height || cursor.rowHeight < 0 || cursor.rowHeight > height - cursor.cursorY
                || count != (long)width * height * channels || count > stream.Length - stream.Position)
                throw new InvalidDataException("Invalid prebaked font atlas page.");
            pixelBytes += count;
            if (pixelBytes > 64L * 1024 * 1024) throw new InvalidDataException("Prebaked font atlas exceeds the pixel limit.");
            pages[index] = new Page(width, height, channels, cursor, (int)stream.Position, count);
            stream.Position += count;
        }

        //读取字符映射及字形度量
        Dictionary<uint, Glyph> glyphs = new(glyphCount);
        for (int index = 0; index < glyphCount; ++index)
        {
            Glyph glyph = new(reader.ReadUInt32(), reader.ReadUInt32(), reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32(),
                reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32(), reader.ReadSingle(), reader.ReadSingle(),
                reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
            if (glyph.Scalar > 0x10ffff || glyph.Scalar is >= 0xd800 and <= 0xdfff || glyph.GlyphIndex == 0
                || !float.IsFinite(glyph.Advance) || !float.IsFinite(glyph.BearingX) || !float.IsFinite(glyph.BearingY)
                || !float.IsFinite(glyph.MetricWidth) || !float.IsFinite(glyph.MetricHeight))
                throw new InvalidDataException("Invalid prebaked font glyph.");
            if (glyph.PageIndex == -1)
            {
                if (glyph.Width != 0 || glyph.Height != 0 || glyph.X != 0 || glyph.Y != 0)
                    throw new InvalidDataException("Invalid empty prebaked font glyph.");
            }
            else if (glyph.PageIndex < 0 || glyph.PageIndex >= pageCount || glyph.Width <= 0 || glyph.Height <= 0
                || glyph.X < 0 || glyph.Width > pages[glyph.PageIndex].Width - glyph.X
                || glyph.Y < 0 || glyph.Height > pages[glyph.PageIndex].Height - glyph.Y)
                throw new InvalidDataException("Prebaked font glyph is outside its atlas page.");
            if (!glyphs.TryAdd(glyph.Scalar, glyph)) throw new InvalidDataException("Duplicate prebaked font character.");
        }
        if (stream.Position != stream.Length) throw new InvalidDataException("Trailing prebaked font atlas data.");
        return new UIFontPrebakedAtlas(data, mode, pixelSize, unitsPerEm, pages, glyphs);
    }
}
