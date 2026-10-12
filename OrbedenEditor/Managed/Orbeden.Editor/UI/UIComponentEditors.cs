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
        Canvas? canvas = Target.Ens.GetComponent<Canvas>();
        if (canvas == null) return;
        if (canvas.GetLayout() == null)
            EditorGUI.Warning("Canvas requires UILayout on the same node; its children cannot render without it.");
        else if (UIWorldContext.Current?.FindNode(canvas.EnsId) is UINode node && node.ConfigurationError.Length != 0)
            EditorGUI.Warning(node.ConfigurationError);
        DrawProperty("enabled");
        if (canvas.GetRenderMode() != CanvasRenderMode.WorldSpace)
        {
            DrawProperty("scaleMode");
            if (canvas.GetScaleMode() == CanvasScaleMode.ReferenceResolution)
            {
                DrawProperty("referenceResolution");
                DrawProperty("matchWidthOrHeight");
            }
            DrawProperty("scaleFactor");
        }

        //离屏才需要输出尺寸与输出纹理。
        if (canvas.GetRenderMode() == CanvasRenderMode.Offscreen)
        {
            DrawProperty("outputSize");
            EditorGUI.Label($"Output Texture: {canvas.GetOutputTexture()?.ResourceKey ?? "(not created)"}");
        }
        //世界空间画布的尺寸就是它自己的大小：在这里明写，省得作者去根节点上找。
        if (canvas.GetRenderMode() == CanvasRenderMode.WorldSpace)
        {
            vector2 size = canvas.GetLayout()?.GetSizeDelta() ?? default;
            EditorGUI.Label($"World Space Size: {size.x:0.#} x {size.y:0.#}");
        }
        //屏幕画布在场景里按根节点缩放显示：逻辑矩形乘根缩放就是它在场景中的大小，场景手柄与聚焦读同一个值。
        else if (canvas.GetRenderMode() == CanvasRenderMode.Overlay)
        {
            vector2 size = canvas.GetLayout()?.GetResolvedRect().size ?? default;
            vector3 scale = canvas.Ens.Transform.GetLocalScale();
            EditorGUI.Label($"Scene Preview Size: {size.x:0.#} x {size.y:0.#} logical units -> {size.x * scale.x:0.###} x {size.y * scale.y:0.###} world units");
        }

        DrawProperty("sortOrder");
        DrawProperty("drawLayer");
        if (canvas.GetRenderMode() == CanvasRenderMode.WorldSpace && canvas.GetDrawLayer() == 0)
            EditorGUI.Warning("Draw Layer 0 excludes this Canvas from every camera. Use 1 for the default layer.");
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
        if (diagnostic.Length != 0) EditorGUI.Warning(diagnostic);
    }
}

/// <summary>布局编辑器：锚点、枢轴、Transform 位置与尺寸，预设修改合并为一次撤销。</summary>
[CustomEditor(typeof(UILayout))]
public sealed class UILayoutEditor : ComponentEditor
{
    private readonly UILayoutGizmos gizmos = new();
    private readonly EditorRectPrimitive[] presetRects = new EditorRectPrimitive[16 * 6];
    private static readonly string[] presetColumns = ["Left", "Center", "Right", "Stretch"];
    private static readonly string[] presetRows = ["Top", "Middle", "Bottom", "Stretch"];

    /// <summary>绘制锚点预设与矩形参数。</summary>
    public override void OnDrawInspector()
    {
        if (Target.Ens.GetComponent<UILayout>() is not UILayout layout) return;
        bool canvasRoot = layout.Ens.GetComponent<Canvas>() != null;
        DrawProperty("enabled");
        //仅对子节点显示相对父矩形的锚点预设
        if (!canvasRoot)
        {
            EditorGUI.Label("Anchor Presets");
            if (UIWorldContext.Current?.FindNode(layout.EnsId)?.Parent?.Layout == null)
                EditorGUI.Warning("No parent UI layout. Alt-click a preset to apply without preserving the rectangle.");
        }
        EditorGUI.BeginDisabled(Targets.Count != 1 || layout.HasDrivenRect);

        if (!canvasRoot) DrawAnchorPresets(layout);

        //按轴显示固定尺寸或拉伸边距
        vector2 offset = layout.GetOffset();
        vector2 size = layout.GetSizeDelta();
        vector2 pivot = layout.GetPivot();
        vector2 minAnchor = layout.GetAnchorMin();
        vector2 maxAnchor = layout.GetAnchorMax();
        bool stretchX = !canvasRoot && minAnchor.x != maxAnchor.x;
        bool stretchY = !canvasRoot && minAnchor.y != maxAnchor.y;
        float x = stretchX ? offset.x - pivot.x * size.x : offset.x;
        float y = stretchY ? -offset.y - (1 - pivot.y) * size.y : offset.y;
        float width = stretchX ? -offset.x - (1 - pivot.x) * size.x : size.x;
        float height = stretchY ? offset.y - pivot.y * size.y : size.y;
        bool edited = EditorGUI.InputFloat(stretchX ? "Left" : "Position X", ref x);
        edited |= EditorGUI.InputFloat(stretchY ? "Top" : "Position Y", ref y);
        EditorGUI.BeginDisabled(layout.GetFitWidth());
        edited |= EditorGUI.InputFloat(stretchX ? "Right" : "Width", ref width);
        EditorGUI.EndDisabled();
        EditorGUI.BeginDisabled(layout.GetFitHeight());
        edited |= EditorGUI.InputFloat(stretchY ? "Bottom" : "Height", ref height);
        EditorGUI.EndDisabled();
        if (edited && float.IsFinite(x) && float.IsFinite(y) && float.IsFinite(width) && float.IsFinite(height))
        {
            size = new vector2(stretchX ? -x - width : MathF.Max(0, width), stretchY ? -y - height : MathF.Max(0, height));
            offset = new vector2(stretchX ? x + pivot.x * size.x : x, stretchY ? height + pivot.y * size.y : y);
            gizmos.Begin(layout);
            layout.SetOffset(offset);
            layout.SetSizeDelta(size);
            gizmos.End("UI Rectangle");
        }
        EditorGUI.BeginDisabled(Targets.Count != 1);
        float z = layout.Ens.Transform.GetLocalPosition().z;
        if (EditorGUI.InputFloat("Position Z", ref z) && float.IsFinite(z))
        {
            gizmos.Begin(layout);
            vector3 position = layout.Ens.Transform.GetLocalPosition();
            layout.Ens.Transform.SetLocalPosition(new vector3(position.x, position.y, z));
            gizmos.End("UI Position Z");
        }
        EditorGUI.EndDisabled();
        EditorGUI.EndDisabled();
        if (Targets.Count != 1) EditorGUI.Warning("Presets and dimensions require a single selection. Edit the raw fields below for multiple objects.");
        if (layout.HasDrivenRect) EditorGUI.Warning("Rectangle is driven by a layout group or control.");
        EditorGUI.Separator();
        //绘制锚点、轴心与布局开关
        if (!canvasRoot)
        {
            DrawProperty("anchorMin");
            DrawProperty("anchorMax");
        }
        DrawProperty("pivot");
        DrawProperty("fitWidth");
        DrawProperty("fitHeight");
        DrawProperty("ignoreLayout");
        UIRect rect = layout.GetResolvedRect();
        EditorGUI.Label($"Resolved Size: {rect.Width:0.##} x {rect.Height:0.##}");
    }

    /// <summary>绘制仅点击的等比锚点网格，尺寸独立于字体并随可用宽度收缩。</summary>
    private void DrawAnchorPresets(UILayout layout)
    {
        const float CellSize = 48;
        const float Gap = 4;
        const float StretchGap = 4;
        const float GridSize = CellSize * 4 + Gap * 3 + StretchGap;
        float scale = MathF.Min(1, MathF.Max(1, NativeEditorGUI.GetContentRegionAvail().x) / GridSize);
        float side = CellSize * scale;
        vector2 origin = EditorGUI.CursorScreenPosition;
        EditorTheme theme = EditorTheme.Current;
        color anchorColor = new(1, 0.65f, 0.3f, 1);
        int count = 0;

        //按统一几何尺寸布置点击区域与预设图标
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                float left = origin.x + (column * (CellSize + Gap) + (column == 3 ? StretchGap : 0)) * scale;
                float top = origin.y + (row * (CellSize + Gap) + (row == 3 ? StretchGap : 0)) * scale;
                vector2 min = new(column == 3 ? 0 : column * 0.5f, row == 3 ? 0 : 1 - row * 0.5f);
                vector2 max = new(column == 3 ? 1 : min.x, row == 3 ? 1 : min.y);
                NativeEditorGUI.SetCursorScreenPos(new vector2(left, top));
                if (NativeEditorGUI.InvisibleButton($"##anchor_{row}_{column}", new vector2(side, side)))
                    ApplyPreset(layout, min, max);
                bool hovered = NativeEditorGUI.IsItemHovered();
                bool selected = layout.GetAnchorMin().Equals(min) && layout.GetAnchorMax().Equals(max);
                if (hovered) EditorGUI.SetTooltip($"{presetRows[row]} / {presetColumns[column]}\nShift: also set pivot\nAlt: also set position / stretch size");

                //绘制圆角卡片与选中边框
                color background = hovered ? theme.Hovered : theme.Header;
                count = EditorRects.Append(presetRects, count, left, top, left + side, top + side,
                    selected ? theme.Active : hovered ? theme.Text : theme.Border);
                presetRects[count - 1].Rounding = 5 * scale;
                float border = (selected ? 2 : 1) * scale;
                count = EditorRects.Append(presetRects, count, left + border, top + border,
                    left + side - border, top + side - border, background);
                presetRects[count - 1].Rounding = 4 * scale;

                //绘制父矩形与锚点；屏幕 Y 方向与布局 Y 方向相反
                float x = left + 12 * scale;
                float y = top + 12 * scale;
                float extent = 24 * scale;
                count = EditorRects.Append(presetRects, count, x, y, x + extent, y + extent, theme.Control);
                count = EditorRects.Append(presetRects, count, x + scale, y + scale,
                    x + extent - scale, y + extent - scale, background);
                float anchorLeft = x + min.x * extent - 2 * scale;
                float anchorTop = y + (1 - max.y) * extent - 2 * scale;
                float anchorRight = x + max.x * extent + 2 * scale;
                float anchorBottom = y + (1 - min.y) * extent + 2 * scale;
                count = EditorRects.Append(presetRects, count, anchorLeft, anchorTop, anchorRight, anchorBottom, anchorColor);
                if (column == 3 && row == 3)
                    count = EditorRects.Append(presetRects, count, anchorLeft + 2 * scale, anchorTop + 2 * scale,
                        anchorRight - 2 * scale, anchorBottom - 2 * scale, background);
            }
        }
        NativeEditorGUI.DrawRects(presetRects, count);

        //保留最后一行的布局高度并恢复左侧起点
        NativeEditorGUI.SetCursorScreenPos(new vector2(origin.x, NativeEditorGUI.GetCursorScreenPos().y));
    }

    /// <summary>绘制场景矩形手柄。</summary>
    public override void OnSceneGui()
    {
        if (Target.Ens.GetComponent<UILayout>() is UILayout layout) gizmos.OnSceneGui(layout);
    }

    //计算预设补偿并记录布局与 Transform 的一次撤销
    private void ApplyPreset(UILayout layout, vector2 min, vector2 max)
    {
        if (layout.Ens.GetComponent<Canvas>() != null) return;
        bool setPivot = EditorInput.IsKeyDown(EditorKey.LeftShift) || EditorInput.IsKeyDown(EditorKey.RightShift);
        bool setPosition = EditorInput.IsKeyDown(EditorKey.LeftAlt) || EditorInput.IsKeyDown(EditorKey.RightAlt);
        vector2 oldMin = layout.GetAnchorMin();
        vector2 oldMax = layout.GetAnchorMax();
        vector2 oldPivot = layout.GetPivot();
        vector2 pivot = setPivot ? new vector2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f) : oldPivot;
        vector2 offset = layout.GetOffset();
        vector2 size = layout.GetSizeDelta();
        UINode? parent = UIWorldContext.Current?.FindNode(layout.EnsId)?.Parent;
        if (!setPosition && parent?.Layout == null) return;
        vector2 parentSize = parent?.Layout?.GetResolvedRect().size ?? default;
        vector2 oldSize = new(parentSize.x * (oldMax.x - oldMin.x) + size.x, parentSize.y * (oldMax.y - oldMin.y) + size.y);
        vector2 nextSize = new(oldSize.x - parentSize.x * (max.x - min.x), oldSize.y - parentSize.y * (max.y - min.y));
        if (setPosition)
        {
            offset = default;
            nextSize = new vector2(min.x != max.x ? 0 : MathF.Max(0, oldSize.x), min.y != max.y ? 0 : MathF.Max(0, oldSize.y));
        }
        else
        {
            offset = new vector2(offset.x + parentSize.x * (oldMin.x - min.x) - oldPivot.x * size.x + pivot.x * nextSize.x,
                offset.y + parentSize.y * (oldMin.y - min.y) - oldPivot.y * size.y + pivot.y * nextSize.y);
        }
        gizmos.Begin(layout);
        layout.SetAnchors(min, max);
        layout.SetPivot(pivot);
        layout.SetOffset(offset);
        layout.SetSizeDelta(nextSize);
        gizmos.End("UI Anchor Preset");
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
    private static string previewText = "Ag";
    private static FontRasterMode previewRasterMode = FontRasterMode.Bitmap;
    //字形框的颜色；用暖色和位图本身的灰阶分开。
    private static readonly color glyphBoxColor = new(1.0f, 0.65f, 0.2f, 0.95f);

    /// <summary>登记字体的资源检视。</summary>
    public static void Register() => EditorAssetInspectors.Register(nameof(Font), Draw);

    /// <summary>绘制字体资源检视；key 是资源 Key。</summary>
    private static void Draw(string key, int objectId)
    {
        _ = objectId;
        Font? font = Resources.Load<Font>(key);
        if (font == null)
        {
            EditorGUI.Label("Font is not loaded. Open a project and reselect this asset.");
            return;
        }

        EditorGUI.Label($"Family: {(font.familyName.Length != 0 ? font.familyName : "(unresolved)")}");
        EditorGUI.Label($"Style: {(font.styleName.Length != 0 ? font.styleName : "(unresolved)")}");
        EditorGUI.Label($"Face Index: {font.faceIndex}");
        EditorGUI.Label($"Units Per Em: {font.unitsPerEm}");
        EditorGUI.Label($"Ascender / Descender / Line Height: {font.ascender} / {font.descender} / {font.lineHeight}");

        EditorGUI.Label("Glyph Preview");
        EditorGUI.InputInt("Pixel Size", ref previewSize);
        previewSize = Math.Clamp(previewSize, FontAtlasCache.MinBitmapPixelSize, FontAtlasCache.MaxBitmapPixelSize);
        EditorGUI.InputText("Sample Text", ref previewText);
        EditorGUI.Label("Raster Mode");
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
            string state = entry.IsMissing ? "missing" : entry.HasPixels ? $"{entry.BitmapWidth}x{entry.BitmapHeight}" : "empty outline";
            EditorGUI.Label($"{mode}: advance {entry.GetAdvance(previewSize):0.##}, bitmap {state}");
        }

        DrawAtlasPreview(font);
        //预览不得把系统字体路径写进资源：这里只读不写。
        EditorGUI.Label("Preview is read-only; no asset fields are written.");
    }

    //图集页预览：把取样文字涉及的页画出来，并框出用到的字形，看得到位图才算真的预览。
    private static void DrawAtlasPreview(Font font)
    {
        const float PageDisplaySize = 256.0f;
        EditorGUI.Label("Atlas Page (sample glyphs outlined)");

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
            EditorGUI.Label("Sample glyphs have no pixels (empty outline or missing); no atlas page to show.");
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
