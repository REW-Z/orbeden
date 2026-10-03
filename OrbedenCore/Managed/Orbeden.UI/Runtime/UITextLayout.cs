using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>水平对齐方式。</summary>
public enum UITextAlignment : uint
{
    /// <summary>靠左对齐。</summary>
    Left = 0,

    /// <summary>居中。</summary>
    Center = 1,

    /// <summary>靠右对齐。</summary>
    Right = 2,
}

/// <summary>垂直对齐方式。</summary>
public enum UITextVerticalAlignment : uint
{
    /// <summary>靠上。</summary>
    Top = 0,

    /// <summary>居中。</summary>
    Center = 1,

    /// <summary>靠下。</summary>
    Bottom = 2,
}

/// <summary>
/// 排版需要的度量来源。字形缓存实现它，用例可以换成假实现：
/// 因此归一化、断行、对齐与脱字符计算都能脱离原生宿主验证。
/// </summary>
public interface IUITextMetrics
{
    /// <summary>批量解析字形；结果与请求一一对应。</summary>
    void ResolveGlyphs(ReadOnlySpan<UIGlyphDemand> demands, Span<UIGlyphEntry?> entries);

    /// <summary>两个字形之间的字距，字体单位；任一方无效时为 0。</summary>
    float GetKerning(Font? font, uint leftGlyph, uint rightGlyph);

    /// <summary>取上升部、行高与 em 单位，均为字体单位；字体为空时给出缺字回退值。</summary>
    void GetFontMetrics(Font? font, out float ascender, out float lineHeight, out int unitsPerEm);
}

/// <summary>一行排版结果。标量范围与字形范围都按行顺序连续。</summary>
public struct UITextLine
{
    /// <summary>行首标量下标。</summary>
    public int firstScalar;

    /// <summary>行内标量数量；空行为 0。</summary>
    public int scalarCount;

    /// <summary>行首字形下标。</summary>
    public int firstGlyph;

    /// <summary>行内字形数量。</summary>
    public int glyphCount;

    /// <summary>行首位置，逻辑像素；对齐偏移已经含在里面。</summary>
    public float startX;

    /// <summary>行宽，逻辑像素；行尾空白不计入。</summary>
    public float width;

    /// <summary>基线位置，逻辑像素，原点在文本块左下角。</summary>
    public float baseline;

    /// <summary>行尾是否由换行符结束。</summary>
    public bool endedWithNewline;

    /// <summary>行的标量结束边界（不含）。</summary>
    public readonly int EndScalar => firstScalar + scalarCount;
}

/// <summary>一个已定位的字形。</summary>
public struct UITextGlyph
{
    /// <summary>Unicode 标量。</summary>
    public uint scalar;

    /// <summary>标量下标。</summary>
    public int scalarIndex;

    /// <summary>字形条目；度量、位图与 Atlas 页都在里面。</summary>
    public UIGlyphEntry entry;

    /// <summary>本字形的水平步进；与前一个字形的字距不在这里，而是落在本字形的原点上。</summary>
    public float advance;

    /// <summary>字形原点，逻辑像素，原点在文本块左下角；已经含与前一个字形的字距。</summary>
    public vector2 position;
}

/// <summary>一次排版的全部结果：行范围、标量映射、字形下标、步进、位置与测量尺寸。</summary>
public sealed class TextLayoutResult
{
    /// <summary>归一化之后的文本。</summary>
    public string text = string.Empty;

    /// <summary>归一化之后的标量序列。</summary>
    public uint[] scalars = [];

    /// <summary>标量边界到 UTF-16 下标的映射；长度是标量数加一。</summary>
    public int[] scalarToUtf16 = [0];

    /// <summary>UTF-16 下标到标量边界的映射；长度是文本长度加一。</summary>
    public int[] utf16ToScalar = [0];

    /// <summary>按行顺序排列的行。</summary>
    public UITextLine[] lines = [];

    /// <summary>按行顺序排列的字形。</summary>
    public UITextGlyph[] glyphs = [];

    /// <summary>测量尺寸，逻辑像素。</summary>
    public vector2 measuredSize;

    /// <summary>排版用的字号。</summary>
    public float fontSize;

    /// <summary>行距，逻辑像素：相邻行基线之间的间隔。</summary>
    public float lineAdvance;

    /// <summary>上升部，逻辑像素。</summary>
    public float ascender;

    /// <summary>是否允许自动换行。</summary>
    public bool wrap;

    /// <summary>标量总数。</summary>
    public int ScalarCount => scalars.Length;

    /// <summary>行数；空文本也有一行。</summary>
    public int LineCount => lines.Length;

    /// <summary>按标量边界取脱字符位置；下标夹紧到有效范围。</summary>
    public vector2 GetCaretPosition(int caret)
    {
        if (lines.Length == 0) return default;
        int index = Math.Clamp(caret, 0, scalars.Length);
        UITextLine line = lines[^1];
        for (int lineIndex = 0; lineIndex < lines.Length; ++lineIndex)
        {
            //边界落在行内就算这一行；软换行处归上一行，换行符之后自然落到下一行。
            if (index > lines[lineIndex].EndScalar) continue;
            line = lines[lineIndex];
            break;
        }

        //字形原点就是它前面的边界，末边界取最后一个字形步进的终点。
        for (int offset = 0; offset < line.glyphCount; ++offset)
        {
            ref readonly UITextGlyph glyph = ref glyphs[line.firstGlyph + offset];
            if (glyph.scalarIndex >= index) return new vector2(glyph.position.x, line.baseline);
        }
        if (line.glyphCount == 0) return new vector2(line.startX, line.baseline);
        ref readonly UITextGlyph last = ref glyphs[line.firstGlyph + line.glyphCount - 1];
        return new vector2(last.position.x + last.advance, line.baseline);
    }

    /// <summary>按点找最近的标量边界；点使用与字形位置相同的坐标系。</summary>
    public int HitTestCaret(vector2 point)
    {
        if (lines.Length == 0) return 0;

        //先按基线找最近的行，再在该行内按相邻步进的中点选边界。
        int nearest = 0;
        float best = float.MaxValue;
        for (int lineIndex = 0; lineIndex < lines.Length; ++lineIndex)
        {
            float distance = MathF.Abs(point.y - lines[lineIndex].baseline);
            if (distance >= best) continue;
            best = distance;
            nearest = lineIndex;
        }

        UITextLine line = lines[nearest];
        for (int offset = 0; offset < line.glyphCount; ++offset)
        {
            ref readonly UITextGlyph glyph = ref glyphs[line.firstGlyph + offset];
            float origin = glyph.position.x;
            float next = offset + 1 < line.glyphCount
                ? glyphs[line.firstGlyph + offset + 1].position.x
                : origin + glyph.advance;
            if (point.x < (origin + next) * 0.5f) return glyph.scalarIndex;
        }
        return line.EndScalar;
    }
}

/// <summary>
/// 文本排版：归一化 → 度量 → 断行 → 对齐。只依赖度量来源，不接触纹理与原生资源。
/// 结果坐标以文本块左下角为原点、Y 轴向上，可直接平移进节点矩形。
/// </summary>
public sealed class UITextLayout
{
    /// <summary>制表位按四个空格推进。</summary>
    public const int TabStopSpaces = 4;

    /// <summary>非法代理项替换成的标量。</summary>
    public const uint ReplacementScalar = 0xFFFD;

    private const uint LineFeed = '\n';
    private const uint CarriageReturn = '\r';
    private const uint Tab = '\t';
    private const uint Space = ' ';

    //行首禁则：这些标量不能出现在行首。
    private const string LineStartForbidden = "，。！？、；：）》】";
    //行尾禁则：这些标量不能出现在行尾。
    private const string LineEndForbidden = "（《【";

    private readonly IUITextMetrics metrics;
    private readonly List<uint> scalars = [];
    private readonly List<int> scalarStarts = [];
    private readonly List<UIGlyphDemand> demands = [];
    private readonly List<UIGlyphEntry?> entries = [];
    private readonly List<UITextLine> lines = [];
    private readonly List<UITextGlyph> glyphs = [];

    private string normalizedText = string.Empty;
    private UIGlyphEntry? spaceEntry;
    private int spaceIndex = -1;
    private float fontSize;
    private float unitsPerLogical;
    private float lineAdvance;
    private float ascender;

    /// <summary>创建排版器。</summary>
    public UITextLayout(IUITextMetrics metrics)
    {
        this.metrics = metrics ?? throw new ArgumentNullException(nameof(metrics));
    }

    /// <summary>
    /// 排版一段文本。availableWidth 不大于零或非有限时视为不限制宽度。
    /// </summary>
    public TextLayoutResult Layout(string? text, Font? font, float requestedFontSize, float availableWidth,
        bool wrap, float lineSpacing, FontRasterMode rasterMode, UITextAlignment horizontal,
        float rasterScale = 1.0f)
    {
        fontSize = float.IsFinite(requestedFontSize) && requestedFontSize > 0.0f ? requestedFontSize : 1.0f;
        float spacing = float.IsFinite(lineSpacing) && lineSpacing >= 0.0f ? lineSpacing : 1.0f;
        float limit = float.IsFinite(availableWidth) && availableWidth > 0.0f ? availableWidth : float.PositiveInfinity;

        Normalize(text);
        ResolveGlyphs(font, rasterMode, fontSize, rasterScale);
        metrics.GetFontMetrics(font, out float ascenderUnits, out float lineHeightUnits, out int unitsPerEm);
        unitsPerLogical = unitsPerEm > 0 ? fontSize / unitsPerEm : 1.0f;
        ascender = ascenderUnits * unitsPerLogical;
        lineAdvance = lineHeightUnits * unitsPerLogical * spacing;
        if (!float.IsFinite(lineAdvance) || lineAdvance <= 0.0f) lineAdvance = fontSize * spacing;

        Breakdown(font, wrap, limit);
        float blockHeight = Math.Max(lines.Count, 1) * lineAdvance;
        float width = Position(font, horizontal, limit, blockHeight);

        return new TextLayoutResult
        {
            text = normalizedText,
            scalars = [.. scalars],
            scalarToUtf16 = [.. scalarStarts],
            utf16ToScalar = BuildUtf16ToScalar(),
            lines = [.. lines],
            glyphs = [.. glyphs],
            measuredSize = new vector2(width, blockHeight),
            fontSize = fontSize,
            lineAdvance = lineAdvance,
            ascender = ascender,
            wrap = wrap,
        };
    }

    //归一化：非法代理项替换 U+FFFD，CRLF 与 CR 归一 LF，同时记录标量到 UTF-16 的边界。
    private void Normalize(string? text)
    {
        scalars.Clear();
        scalarStarts.Clear();
        normalizedText = string.Empty;
        if (string.IsNullOrEmpty(text)) return;

        string source = text!;
        System.Text.StringBuilder builder = new(source.Length);
        for (int index = 0; index < source.Length; ++index)
        {
            char current = source[index];
            if (current == CarriageReturn)
            {
                if (index + 1 < source.Length && source[index + 1] == '\n') ++index;
                Append(LineFeed);
                continue;
            }

            if (char.IsHighSurrogate(current) && index + 1 < source.Length && char.IsLowSurrogate(source[index + 1]))
            {
                Append((uint)char.ConvertToUtf32(current, source[index + 1]));
                ++index;
                continue;
            }

            //落单的代理项不是合法标量，统一替换。
            Append(char.IsSurrogate(current) ? ReplacementScalar : current);
        }
        scalarStarts.Add(builder.Length);
        normalizedText = builder.ToString();
        return;

        void Append(uint scalar)
        {
            scalarStarts.Add(builder.Length);
            scalars.Add(scalar);
            builder.Append(char.ConvertFromUtf32((int)scalar));
        }
    }

    //批量解析全部标量的字形，外加一个空格字形供制表位使用。
    //位图字号 = 逻辑字号 × 光栅缩放：屏幕画布按画布缩放取像素，世界空间按相机投影后的局部字高取。
    private void ResolveGlyphs(Font? font, FontRasterMode rasterMode, float size, float rasterScale)
    {
        float scale = float.IsFinite(rasterScale) && rasterScale > 0.0f ? rasterScale : 1.0f;
        int pixelSize = rasterMode == FontRasterMode.Bitmap
            ? Math.Clamp((int)MathF.Round(size * scale),
                FontAtlasCache.MinBitmapPixelSize, FontAtlasCache.MaxBitmapPixelSize)
            : 0;

        demands.Clear();
        for (int index = 0; index < scalars.Count; ++index)
            demands.Add(new UIGlyphDemand(font, scalars[index], rasterMode, pixelSize));
        spaceIndex = demands.Count;
        demands.Add(new UIGlyphDemand(font, Space, rasterMode, pixelSize));

        while (entries.Count < demands.Count) entries.Add(null);
        metrics.ResolveGlyphs(CollectionsMarshal.AsSpan(demands), CollectionsMarshal.AsSpan(entries));
        spaceEntry = entries[spaceIndex];
    }

    //断行：先按宽度扫出每行能容纳的范围，再在范围内挑合法断点。每次循环必定消费至少一个标量。
    private void Breakdown(Font? font, bool wrap, float limit)
    {
        lines.Clear();
        int count = scalars.Count;
        int lineStart = 0;
        while (true)
        {
            float pen = 0.0f;
            int scan = lineStart;
            int lastSoft = -1;
            uint previousGlyph = 0;
            while (scan < count && scalars[scan] != LineFeed)
            {
                float advance = AdvanceInLine(font, scan, ref previousGlyph, ref pen);
                //行首标量再宽也置入本行，保证每行至少消费一个标量。
                if (wrap && scan > lineStart && pen + advance > limit) break;
                pen += advance;
                ++scan;
                if (IsSoftBreak(scan, count) && IsLegalBreak(lineStart, scan, count)) lastSoft = scan;
            }

            bool atNewline = scan < count && scalars[scan] == LineFeed;
            int end = scan;
            if (!atNewline && scan < count)
            {
                //被宽度截断：优先退到最后的合法软断点，没有就按标量硬断（超长词、无空白文本）。
                end = lastSoft > lineStart ? lastSoft : scan;
            }
            if (end < lineStart) end = lineStart;

            lines.Add(new UITextLine
            {
                firstScalar = lineStart,
                scalarCount = end - lineStart,
                endedWithNewline = atNewline,
            });

            if (atNewline)
            {
                //换行符之后还有标量才有下一行；以换行符结尾时不多出空行。
                lineStart = end + 1;
                if (lineStart >= count) break;
                continue;
            }
            lineStart = end;
            if (lineStart >= count) break;
        }

        if (lines.Count == 0) lines.Add(new UITextLine());
    }

    //定位：逐行推进笔位、套用对齐偏移，再把基线换算到块左下角原点。返回最大行宽。
    private float Position(Font? font, UITextAlignment horizontal, float limit, float blockHeight)
    {
        glyphs.Clear();
        float maximumWidth = 0.0f;
        for (int lineIndex = 0; lineIndex < lines.Count; ++lineIndex)
        {
            UITextLine line = lines[lineIndex];
            int firstGlyph = glyphs.Count;
            float pen = 0.0f;
            float visualWidth = 0.0f;
            uint previousGlyph = 0;
            for (int index = line.firstScalar; index < line.EndScalar; ++index)
            {
                uint scalar = scalars[index];
                if (scalar == LineFeed || scalar == CarriageReturn) continue;

                UIGlyphEntry? entry = index < entries.Count ? entries[index] : null;
                float advance;
                float kerning = 0.0f;
                if (scalar == Tab)
                {
                    advance = TabAdvance(pen);
                    previousGlyph = 0;
                }
                else
                {
                    advance = entry?.GetAdvance(fontSize) ?? 0.0f;
                    //字距只作用于同一字体内相邻的有效字形；缺字与控制字符清空前字形。
                    if (previousGlyph != 0 && entry != null && entry.GlyphIndex != 0)
                        kerning = metrics.GetKerning(font, previousGlyph, entry.GlyphIndex) * unitsPerLogical;
                    previousGlyph = entry?.GlyphIndex ?? 0;
                }

                //字距落在本字形的原点上：前一个字形的步进不含它，笔位照旧。
                float originX = pen + kerning;
                glyphs.Add(new UITextGlyph
                {
                    scalar = scalar,
                    scalarIndex = index,
                    entry = entry ?? MissingEntry,
                    advance = advance,
                    position = new vector2(originX, 0.0f),
                });
                pen = originX + advance;
                //行尾空白折叠：不计入视觉宽度。
                if (!IsSpace(scalar)) visualWidth = pen;
            }

            float offset = AlignmentOffset(horizontal, limit, visualWidth);
            if (offset != 0.0f)
            {
                for (int index = firstGlyph; index < glyphs.Count; ++index)
                {
                    UITextGlyph glyph = glyphs[index];
                    glyph.position = new vector2(glyph.position.x + offset, 0.0f);
                    glyphs[index] = glyph;
                }
            }

            line.firstGlyph = firstGlyph;
            line.glyphCount = glyphs.Count - firstGlyph;
            line.startX = offset;
            line.width = visualWidth;
            line.baseline = blockHeight - (ascender + lineIndex * lineAdvance);
            lines[lineIndex] = line;
            if (visualWidth > maximumWidth) maximumWidth = visualWidth;
        }
        return maximumWidth;
    }

    //扫描阶段的单标量步进：含字距，制表符按制表位推进。
    private float AdvanceInLine(Font? font, int index, ref uint previousGlyph, ref float pen)
    {
        uint scalar = scalars[index];
        if (scalar == CarriageReturn) return 0.0f;
        if (scalar == Tab)
        {
            previousGlyph = 0;
            return TabAdvance(pen);
        }

        UIGlyphEntry? entry = index < entries.Count ? entries[index] : null;
        float advance = entry?.GetAdvance(fontSize) ?? 0.0f;
        if (previousGlyph != 0 && entry != null && entry.GlyphIndex != 0)
            advance += metrics.GetKerning(font, previousGlyph, entry.GlyphIndex) * unitsPerLogical;
        previousGlyph = entry?.GlyphIndex ?? 0;
        return advance;
    }

    //制表符前进到下一个制表位；制表位按四个空格宽度对齐，从行首算起。
    private float TabAdvance(float pen)
    {
        float space = spaceEntry?.GetAdvance(fontSize) ?? fontSize * 0.5f;
        float stop = space * TabStopSpaces;
        if (stop <= 0.0f) return 0.0f;
        return stop - pen % stop;
    }

    private static float AlignmentOffset(UITextAlignment alignment, float limit, float width)
    {
        if (!float.IsFinite(limit)) return 0.0f;
        float free = limit - width;
        if (free <= 0.0f) return 0.0f;
        return alignment switch
        {
            UITextAlignment.Center => free * 0.5f,
            UITextAlignment.Right => free,
            _ => 0.0f,
        };
    }

    private static bool IsSpace(uint scalar) =>
        scalar == Space || scalar == Tab || scalar == 0x00A0 || scalar == 0x3000;

    //软断点：空白之后，以及汉字前后，都可以断行。
    private bool IsSoftBreak(int index, int count)
    {
        if (index <= 0 || index >= count) return false;
        uint previous = scalars[index - 1];
        uint current = scalars[index];
        return IsSpace(previous) || IsCjk(previous) || IsCjk(current);
    }

    //禁则：行首不能是收尾标点，行尾不能是起首标点。
    private bool IsLegalBreak(int start, int index, int count)
    {
        if (index <= start) return false;
        if (index >= count) return true;
        if (LineStartForbidden.IndexOf((char)scalars[index]) >= 0) return false;
        if (LineEndForbidden.IndexOf((char)scalars[index - 1]) >= 0) return false;
        return true;
    }

    private static bool IsCjk(uint scalar) =>
        (scalar >= 0x2E80 && scalar <= 0x9FFF)     // 部首、假名、汉字
        || (scalar >= 0xAC00 && scalar <= 0xD7AF)  // 谚文音节
        || (scalar >= 0xF900 && scalar <= 0xFAFF)  // 兼容汉字
        || (scalar >= 0xFF00 && scalar <= 0xFF60); // 全角标点

    private int[] BuildUtf16ToScalar()
    {
        int length = normalizedText.Length;
        int[] map = new int[length + 1];
        for (int scalarIndex = 0; scalarIndex < scalars.Count; ++scalarIndex)
        {
            int start = scalarStarts[scalarIndex];
            int end = scalarStarts[scalarIndex + 1];
            for (int index = start; index < end && index <= length; ++index) map[index] = scalarIndex;
        }
        map[length] = scalars.Count;
        return map;
    }

    //度量来源应当为每个请求给出条目；真的拿到空项时按缺字处理，保证位置数组不留空。
    private static readonly UIGlyphEntry FallbackMissing = UIGlyphEntry.CreateMissing();

    private static UIGlyphEntry MissingEntry => FallbackMissing;
}
