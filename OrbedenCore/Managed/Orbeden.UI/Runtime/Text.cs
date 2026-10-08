using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 文本图形。排版结果缓存到输入或可用宽度变化为止；字形像素与 Atlas 页由字形缓存负责，
/// 页被回收后靠代次变化触发重排与重建，不复制像素、也不自己管理纹理。
/// </summary>
public class Text : UIVisual
{
    [SerializeField] private Font? font;
    [SerializeField] private string text = string.Empty;
    [SerializeField] private float fontSize = 16.0f;
    [SerializeField] private FontRasterMode rasterMode = FontRasterMode.Bitmap;
    [SerializeField] private bool wrap;
    [SerializeField] private float lineSpacing = 1.0f;
    [SerializeField] private UITextAlignment horizontalAlignment = UITextAlignment.Left;
    [SerializeField] private UITextVerticalAlignment verticalAlignment = UITextVerticalAlignment.Top;

    private readonly UITextLayout layoutBuilder = new(FontAtlasCache.Shared);
    //几何用到的页代次：任何一页被回收都要重建。
    private readonly List<(UIGlyphEntry entry, uint generation)> glyphPages = [];
    //本帧用到的纹理，按首次出现顺序排列，避免一段文字在页之间来回切片段。
    private readonly List<Texture2D?> pageOrder = [];

    //缓存共享排版结果
    private TextLayoutResult? layout;
    private LayoutKey layoutKey;

    /// <summary>创建文本组件包装；文字默认不阻挡指针。</summary>
    public Text(Ens ens) : base(ens)
    {
        SetRaycastTarget(false);
    }

    /// <summary>显式字体资源；空表示使用内置默认字体。</summary>
    public Font? GetFont() => font;

    /// <summary>获取实际排版字体；未指定时使用内置默认字体。</summary>
    public Font? GetEffectiveFont() => font ?? UIWorldContext.Current?.GetDefaultFont();

    /// <summary>设置字体；字体换了必须重排。</summary>
    public void SetFont(Font? value)
    {
        if (ReferenceEquals(font, value)) return;
        font = value;
        InvalidateLayout();
    }

    /// <summary>文本内容。</summary>
    public string GetText() => text;

    /// <summary>设置文本内容。</summary>
    public void SetText(string? value)
    {
        string next = value ?? string.Empty;
        if (text == next) return;
        text = next;
        InvalidateLayout();
    }

    /// <summary>字号，逻辑像素。</summary>
    public float GetFontSize() => fontSize;

    /// <summary>设置字号；小于 1 的值夹紧到 1。</summary>
    public void SetFontSize(float value)
    {
        float next = float.IsFinite(value) && value > 1.0f ? value : 1.0f;
        if (fontSize == next) return;
        fontSize = next;
        InvalidateLayout();
    }

    /// <summary>光栅化模式。</summary>
    public FontRasterMode GetRasterMode() => rasterMode;

    /// <summary>设置光栅化模式；换模式等于换字形缓存键。</summary>
    public void SetRasterMode(FontRasterMode value)
    {
        if (rasterMode == value) return;
        rasterMode = value;
        InvalidateLayout();
    }

    /// <summary>是否自动换行。</summary>
    public bool GetWrap() => wrap;

    /// <summary>设置是否自动换行。</summary>
    public void SetWrap(bool value)
    {
        if (wrap == value) return;
        wrap = value;
        InvalidateLayout();
    }

    /// <summary>行距倍数。</summary>
    public float GetLineSpacing() => lineSpacing;

    /// <summary>设置行距倍数；负值与非有限值拒绝写入。</summary>
    public void SetLineSpacing(float value)
    {
        if (!float.IsFinite(value) || value < 0.0f || lineSpacing == value) return;
        lineSpacing = value;
        InvalidateLayout();
    }

    /// <summary>水平对齐。</summary>
    public UITextAlignment GetHorizontalAlignment() => horizontalAlignment;

    /// <summary>设置水平对齐。</summary>
    public void SetHorizontalAlignment(UITextAlignment value)
    {
        if (horizontalAlignment == value) return;
        horizontalAlignment = value;
        InvalidateLayout();
    }

    /// <summary>垂直对齐。</summary>
    public UITextVerticalAlignment GetVerticalAlignment() => verticalAlignment;

    /// <summary>设置垂直对齐；只平移结果，不重排。</summary>
    public void SetVerticalAlignment(UITextVerticalAlignment value)
    {
        if (verticalAlignment == value) return;
        verticalAlignment = value;
        SetVerticesDirty();
    }

    /// <summary>测量宽度：不换行时的自然宽度。</summary>
    public override float MeasureWidth() =>
        BuildLayout(0.0f, wrapEnabled: false).measuredSize.x;

    /// <summary>测量高度：按给定可用宽度换行后的块高。</summary>
    public override float MeasureHeight(float availableWidth) =>
        BuildLayout(availableWidth, wrap).measuredSize.y;

    /// <summary>当前排版结果；没有排版过时为空。</summary>
    public TextLayoutResult? GetLayoutResult() => layout;

    /// <summary>按标量边界取脱字符位置，坐标与节点本地空间一致；没有排版结果时返回零。</summary>
    public vector2 GetCaretPosition(int caret)
    {
        TextLayoutResult? result = layout;
        if (result == null) return default;
        vector2 origin = LayoutOrigin(result);
        vector2 local = result.GetCaretPosition(caret);
        return new vector2(origin.x + local.x, origin.y + local.y);
    }

    /// <summary>按节点本地坐标找最近的标量边界；没有排版结果时返回零。</summary>
    public int HitTestCaret(vector2 localPoint)
    {
        TextLayoutResult? result = layout;
        if (result == null) return 0;
        vector2 origin = LayoutOrigin(result);
        return result.HitTestCaret(new vector2(localPoint.x - origin.x, localPoint.y - origin.y));
    }

    //检查字形资源与画布光栅缩放
    protected internal override bool IsGeometryInvalidated()
    {
        float scale = UIWorldContext.Current?.GetRasterScale(GetCanvas()) ?? 1.0f;
        if (layout == null || !SameScale(layoutKey.RasterScale, scale)) return true;

        for (int index = 0; index < glyphPages.Count; ++index)
        {
            if (glyphPages[index].entry.PageGeneration != glyphPages[index].generation) return true;
        }
        return false;
    }

    //缩放比较留一点容差：画布缩放每帧重算，浮点噪声不该引起整段文字反复重排。
    private static bool SameScale(float left, float right) =>
        MathF.Abs(left - right) <= MathF.Max(MathF.Abs(left), MathF.Abs(right)) * 1e-4f;

    /// <summary>生成文本网格：按页分组提交四边形，缺字画四条矩形组成的方框。</summary>
    protected override void PopulateMesh(UIMeshBuilder mesh)
    {
        glyphPages.Clear();
        UIRect rect = GetLayout()?.GetResolvedRect() ?? new UIRect(new vector2(0.0f, 0.0f), new vector2(0.0f, 0.0f));
        TextLayoutResult result = GetOrBuildLayout(rect.Width, mesh.ViewScale);

        //定位：结果以块左下角为原点，垂直对齐只平移整块。
        vector2 origin = LayoutOrigin(result);

        //记录本次几何依赖的页代次，页被回收后据此重建。
        glyphPages.Clear();
        pageOrder.Clear();
        foreach (UITextGlyph glyph in result.glyphs)
        {
            if (!glyph.entry.HasPixels) continue;
            glyphPages.Add((glyph.entry, glyph.entry.PageGeneration));
            if (!ContainsTexture(glyph.entry.Texture)) pageOrder.Add(glyph.entry.Texture);
        }

        for (int pageIndex = 0; pageIndex < pageOrder.Count; ++pageIndex)
        {
            Texture2D? texture = pageOrder[pageIndex];
            mesh.SetTexture(texture, MaterialKind);
            if (rasterMode != FontRasterMode.Bitmap) mesh.SetDistanceRange(FontAtlasCache.DistanceFieldRange);
            color white = new(1.0f, 1.0f, 1.0f, 1.0f);
            bool flipY = texture != null && !texture.IsRenderTarget();
            foreach (UITextGlyph glyph in result.glyphs)
            {
                if (!glyph.entry.HasPixels || !ReferenceEquals(glyph.entry.Texture, texture)) continue;
                vector2 pen = new(origin.x + glyph.position.x, origin.y + glyph.position.y);
                if (!glyph.entry.TryGetTargetRect(pen, result.fontSize, out UIRect target)) continue;
                mesh.AddQuad(target, UIMeshBuilder.ToTextureUv(glyph.entry.Uv.min, flipY),
                    UIMeshBuilder.ToTextureUv(glyph.entry.Uv.Max, flipY), white);
            }
        }

        DrawMissingGlyphs(mesh, result, origin);
    }

    /// <summary>逐片段设置材质种类；纹理按页在 PopulateMesh 里切换。</summary>
    protected internal override void ModifyDrawState(ref UIDrawState state)
    {
        state.materialKind = MaterialKind;
        if (rasterMode != FontRasterMode.Bitmap) state.distanceRange = FontAtlasCache.DistanceFieldRange;
    }

    private UIMaterialKind MaterialKind => rasterMode switch
    {
        FontRasterMode.SDF => UIMaterialKind.SDF,
        FontRasterMode.MSDF => UIMaterialKind.MSDF,
        _ => UIMaterialKind.Bitmap,
    };

    //缺字方框：按 em 比例画四条矩形，不依赖字体光栅化，用引擎白纹理。
    private void DrawMissingGlyphs(UIMeshBuilder mesh, TextLayoutResult result, vector2 origin)
    {
        bool started = false;
        color white = new(1.0f, 1.0f, 1.0f, 1.0f);
        vector2 uvMin = new(0.0f, 0.0f);
        vector2 uvMax = new(1.0f, 1.0f);
        foreach (UITextGlyph glyph in result.glyphs)
        {
            if (!glyph.entry.IsMissing) continue;
            if (!started)
            {
                //空纹理就是引擎白纹理，方框走普通图片那一支。
                mesh.SetTexture(null, UIMaterialKind.ImageStraight);
                started = true;
            }

            vector2 pen = new(origin.x + glyph.position.x, origin.y + glyph.position.y);
            vector2 box = glyph.entry.GetBoxSize(result.fontSize);
            float stroke = FontAtlasCache.MissingStrokeEm * result.fontSize;
            //方框在步进内居中，纵向落在基线上方。
            float left = pen.x + (glyph.advance - box.x) * 0.5f;
            float bottom = pen.y;
            float top = bottom + box.y;

            mesh.AddQuad(new UIRect(new vector2(left, top - stroke), new vector2(box.x, stroke)), uvMin, uvMax, white);
            mesh.AddQuad(new UIRect(new vector2(left, bottom), new vector2(box.x, stroke)), uvMin, uvMax, white);
            mesh.AddQuad(new UIRect(new vector2(left, bottom), new vector2(stroke, box.y)), uvMin, uvMax, white);
            mesh.AddQuad(new UIRect(new vector2(left + box.x - stroke, bottom), new vector2(stroke, box.y)), uvMin, uvMax, white);
        }
    }

    //按引用比较纹理，避免包装类型自带的相等语义把两页当成同一张。
    private bool ContainsTexture(Texture2D? texture)
    {
        for (int index = 0; index < pageOrder.Count; ++index)
        {
            if (ReferenceEquals(pageOrder[index], texture)) return true;
        }
        return false;
    }

    //排版结果以块左下角为原点：水平方向由排版处理，垂直方向在这里平移。
    private vector2 LayoutOrigin(TextLayoutResult result)
    {
        UIRect rect = GetLayout()?.GetResolvedRect() ?? new UIRect(new vector2(0.0f, 0.0f), new vector2(0.0f, 0.0f));
        float blockTop = verticalAlignment switch
        {
            UITextVerticalAlignment.Center => rect.Center.y + result.measuredSize.y * 0.5f,
            UITextVerticalAlignment.Bottom => rect.min.y + result.measuredSize.y,
            _ => rect.Max.y,
        };
        return new vector2(rect.min.x, blockTop - result.measuredSize.y);
    }

    //按节点矩形宽度取排版结果；输入、宽度或光栅缩放变了都要重排。
    //光栅缩放只影响字形位图尺寸与目标矩形，步进与行高仍按逻辑字号算，所以布局结果可以整块缓存。
    //读取或重建共享排版
    private TextLayoutResult GetOrBuildLayout(float width, float rasterScale)
    {
        float scale = float.IsFinite(rasterScale) && rasterScale > 0.0f ? rasterScale : 1.0f;
        Font? effectiveFont = GetEffectiveFont();
        LayoutKey key = new(text, effectiveFont, width, fontSize, lineSpacing, rasterMode, wrap, horizontalAlignment, scale);
        if (layout != null && layoutKey.Equals(key)) return layout;
        layout = layoutBuilder.Layout(text, effectiveFont, fontSize, width, wrap,
            lineSpacing, rasterMode, horizontalAlignment, scale);
        layoutKey = key;
        return layout;
    }

    //一次排版的输入；缓存命中要求全部一致。
    private readonly record struct LayoutKey(
        string Text, Font? Font, float Width, float FontSize, float LineSpacing,
        FontRasterMode Mode, bool Wrap, UITextAlignment Alignment, float RasterScale);

    //测量只关心步进与行高，与光栅缩放无关，固定按 1 排版。
    private TextLayoutResult BuildLayout(float width, bool wrapEnabled) =>
        layoutBuilder.Layout(text, GetEffectiveFont(), fontSize, width, wrapEnabled, lineSpacing, rasterMode,
            horizontalAlignment);

    private void InvalidateLayout()
    {
        layout = null;
        SetLayoutDirty();
        SetVerticesDirty();
    }
}
