using System;
using System.Collections.Generic;
using System.Text;
using Orbeden;
using OrbedenEditor;

namespace OrbedenEditor;

/// <summary>画布编辑器：渲染方式、缩放方式与输出目标。</summary>
[CustomEditor(typeof(Canvas))]
public sealed class CanvasEditor : ComponentEditor
{
    /// <summary>绘制画布检视面板。</summary>
    public override void OnDrawInspector()
    {
        DrawProperty("renderMode");
        DrawProperty("scaleMode");
        DrawProperty("referenceResolution");
        DrawProperty("matchWidthOrHeight");
        DrawProperty("scaleFactor");

        Canvas? canvas = Target.Ens.GetComponent<Canvas>();
        if (canvas == null) return;

        //离屏才需要输出尺寸与输出纹理。
        if (canvas.GetRenderMode() == CanvasRenderMode.Offscreen)
        {
            DrawProperty("outputSize");
            EditorGUI.Label($"输出纹理：{canvas.GetOutputTexture()?.ResourceKey ?? "(尚未创建)"}");
        }
        //世界空间画布的尺寸就是它自己的大小：在这里明写，省得作者去根节点上找。
        if (canvas.GetRenderMode() == CanvasRenderMode.WorldSpace)
        {
            vector2 size = canvas.GetLayout()?.GetSizeDelta() ?? default;
            EditorGUI.Label($"世界空间尺寸：{size.x:0.#} × {size.y:0.#}（逻辑单位，缩放取节点 Transform）");
        }

        DrawProperty("sortOrder");
        DrawProperty("drawLayer");
    }
}

/// <summary>遮罩编辑器：配置不完整时把诊断显示出来。</summary>
[CustomEditor(typeof(Mask))]
public sealed class MaskEditor : ComponentEditor
{
    /// <summary>绘制遮罩检视面板。</summary>
    public override void OnDrawInspector()
    {
        DrawProperty("mode");
        DrawProperty("texture");
        DrawProperty("uvMin");
        DrawProperty("uvMax");
        DrawProperty("hitTestThreshold");

        if (Target.Ens.GetComponent<Mask>() is not Mask mask) return;
        //配置不完整时覆盖率为零：在面板上明说，不让作者去猜为什么看不到内容。
        string diagnostic = mask.GetDiagnostic();
        if (diagnostic.Length != 0) EditorGUI.Label(diagnostic);
    }
}

/// <summary>布局编辑器：锚点、枢轴、偏移与尺寸，外加一次提交五组字段的锚点预设。</summary>
[CustomEditor(typeof(UILayout))]
public sealed class UILayoutEditor : ComponentEditor
{
    //锚点预设：一次提交五组布局字段，撤销也只算一步。
    private static readonly (string Label, vector2 Min, vector2 Max, vector2 Pivot)[] Presets =
    [
        ("左上", new vector2(0.0f, 1.0f), new vector2(0.0f, 1.0f), new vector2(0.0f, 1.0f)),
        ("居中", new vector2(0.5f, 0.5f), new vector2(0.5f, 0.5f), new vector2(0.5f, 0.5f)),
        ("右下", new vector2(1.0f, 0.0f), new vector2(1.0f, 0.0f), new vector2(1.0f, 0.0f)),
        ("横向拉伸", new vector2(0.0f, 0.5f), new vector2(1.0f, 0.5f), new vector2(0.5f, 0.5f)),
        ("双轴拉伸", new vector2(0.0f, 0.0f), new vector2(1.0f, 1.0f), new vector2(0.5f, 0.5f)),
    ];

    //每个编辑器实例持有自己的拖动状态：多选同一类型时互不干扰。
    private readonly UILayoutGizmos gizmos = new();

    /// <summary>绘制布局检视面板。</summary>
    public override void OnDrawInspector()
    {
        EditorGUI.Label("锚点预设");
        EditorGUI.SameLine();
        foreach ((string label, vector2 min, vector2 max, vector2 pivot) in Presets)
        {
            if (!EditorGUI.Button(label)) continue;
            ApplyPreset(min, max, pivot);
        }

        DrawDefaultInspector();
    }

    //选中时在场景里画矩形手柄；拖动改字段、松手记一条撤销。
    /// <summary>在场景里绘制布局矩形与手柄。</summary>
    public override void OnSceneGui()
    {
        UILayout? layout = Target.Ens.GetComponent<UILayout>();
        if (layout == null) return;
        gizmos.OnSceneGui(layout);
    }

    //预设一次提交五组字段：锚点、枢轴与偏移一起改，撤销只记一步。
    private void ApplyPreset(vector2 min, vector2 max, vector2 pivot)
    {
        UILayout? layout = Target.Ens.GetComponent<UILayout>();
        if (layout == null) return;

        string label = $"UI Layout 锚点预设";
        Vector2Capture before = Vector2Capture.Of(layout);
        layout.SetAnchorMin(min);
        layout.SetAnchorMax(max);
        layout.SetPivot(pivot);
        layout.SetOffset(new vector2(0.0f, 0.0f));
        Vector2Capture after = Vector2Capture.Of(layout);

        EditorPropertyHistory.RecordAction(label, () => before.Apply(layout), () => after.Apply(layout));
    }

    //一组布局向量的快照；撤销重做只依赖它，不闭包保存可能失效的包装。
    private readonly struct Vector2Capture(
        vector2 anchorMin, vector2 anchorMax, vector2 pivot, vector2 offset, vector2 sizeDelta)
    {
        internal static Vector2Capture Of(UILayout layout) => new(
            layout.GetAnchorMin(), layout.GetAnchorMax(), layout.GetPivot(), layout.GetOffset(), layout.GetSizeDelta());

        internal void Apply(UILayout layout)
        {
            layout.SetAnchorMin(anchorMin);
            layout.SetAnchorMax(anchorMax);
            layout.SetPivot(pivot);
            layout.SetOffset(offset);
            layout.SetSizeDelta(sizeDelta);
        }
    }
}

/// <summary>
/// 字体编辑器：字体面下标、解析出来的元数据与三种模式的光栅结果。
/// 字体是资源不是组件，因此走资源检视扩展点，按 Key 取对象。
/// </summary>
public static class FontEditor
{
    //预览用的字号、取样文本与光栅模式；只在面板上临时存在，不写进资源。
    private static int previewSize = 48;
    private static string previewText = "Ag汉";
    private static FontRasterMode previewRasterMode = FontRasterMode.Bitmap;
    //字形框的颜色；用暖色和位图本身的灰阶分开。
    private static readonly color glyphBoxColor = new(1.0f, 0.65f, 0.2f, 0.95f);

    /// <summary>登记字体的资源检视。</summary>
    public static void Register()
    {
        EditorAssetInspectors.Register(nameof(Font), Draw);
        ManagedAssemblySession.RegisterUnloadHandler(() => EditorAssetInspectors.Unregister(nameof(Font)));
    }

    /// <summary>绘制字体资源检视；key 是资源 Key。</summary>
    private static void Draw(string key, int objectId)
    {
        _ = objectId;
        Font? font = Resources.Load<Font>(key);
        if (font == null)
        {
            EditorGUI.Label("字体尚未加载：打开项目后重新选中这个资源。");
            return;
        }

        EditorGUI.Label($"字体族：{(font.familyName.Length != 0 ? font.familyName : "(未解析)")}");
        EditorGUI.Label($"样式：{(font.styleName.Length != 0 ? font.styleName : "(未解析)")}");
        EditorGUI.Label($"字体面下标：{font.faceIndex}");
        EditorGUI.Label($"每 em 单位：{font.unitsPerEm}");
        EditorGUI.Label($"上升部/下降部/行高：{font.ascender} / {font.descender} / {font.lineHeight}");

        EditorGUI.Label("字形预览");
        EditorGUI.InputInt("字号", ref previewSize);
        previewSize = Math.Clamp(previewSize, FontAtlasCache.MinBitmapPixelSize, FontAtlasCache.MaxBitmapPixelSize);
        EditorGUI.InputText("取样", ref previewText);
        EditorGUI.Label("光栅模式");
        EditorGUI.SameLine();
        foreach (FontRasterMode mode in new[] { FontRasterMode.Bitmap, FontRasterMode.SDF, FontRasterMode.MSDF })
        {
            if (!EditorGUI.ToggleButton(mode.ToString(), previewRasterMode == mode)) continue;
            previewRasterMode = mode;
        }

        //三种模式各取一次度量与位图，放在一起比较。
        foreach (FontRasterMode mode in new[] { FontRasterMode.Bitmap, FontRasterMode.SDF, FontRasterMode.MSDF })
        {
            uint scalar = previewText.Length > 0 ? previewText[0] : 'A';
            UIGlyphEntry entry = FontAtlasCache.Shared.RequestGlyph(font, scalar, mode, previewSize);
            string state = entry.IsMissing ? "缺字" : entry.HasPixels ? $"{entry.BitmapWidth}×{entry.BitmapHeight}" : "空轮廓";
            EditorGUI.Label($"{mode}：步进 {entry.GetAdvance(previewSize):0.##}，位图 {state}");
        }

        DrawAtlasPreview(font);
        //预览不得把系统字体路径写进资源：这里只读不写。
        EditorGUI.Label("预览只读，不会改写资源里的任何字段。");
    }

    //图集页预览：把取样文字涉及的页画出来，并框出用到的字形，看得到位图才算真的预览。
    private static void DrawAtlasPreview(Font font)
    {
        const float PageDisplaySize = 256.0f;
        EditorGUI.Label("图集页（框出取样字形）");

        List<(UIGlyphEntry Entry, uint Scalar)> glyphs = [];
        Texture2D? page = null;
        string source = previewText.Length != 0 ? previewText : "A";
        foreach (Rune rune in source.EnumerateRunes())
        {
            UIGlyphEntry entry = FontAtlasCache.Shared.RequestGlyph(font, (uint)rune.Value, previewRasterMode, previewSize);
            if (!entry.HasPixels) continue;
            //一页一张图：取样跨页时只画第一页，框出的也只在同一页里。
            if (page == null) page = entry.Texture;
            else if (!ReferenceEquals(page, entry.Texture)) continue;
            glyphs.Add((entry, (uint)rune.Value));
        }

        if (page == null)
        {
            EditorGUI.Label("取样字形没有像素（空轮廓或缺字），没有可显示的图集页。");
            return;
        }

        //先取光标位再画图：覆盖框必须和图片落在同一块屏幕区域上。
        vector2 origin = EditorGUI.CursorScreenPosition;
        EditorGUI.DrawTexture(page, new vector2(PageDisplaySize, PageDisplaySize));
        foreach ((UIGlyphEntry entry, uint scalar) in glyphs)
        {
            vector2 min = new(origin.x + entry.Uv.min.x * PageDisplaySize, origin.y + (1.0f - entry.Uv.Max.y) * PageDisplaySize);
            vector2 max = new(origin.x + entry.Uv.Max.x * PageDisplaySize, origin.y + (1.0f - entry.Uv.min.y) * PageDisplaySize);
            EditorGUI.DrawRectOutline(min, max, glyphBoxColor);
        }
    }
}
