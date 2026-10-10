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
            EditorGUI.TextWrapped("Canvas requires UILayout on the same node. Add UILayout to this Canvas; its children cannot render without it.");
        else if (UIWorldContext.Current?.FindNode(canvas.EnsId) is UINode node && node.ConfigurationError.Length != 0)
            EditorGUI.TextWrapped(node.ConfigurationError);
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
            EditorGUI.Label($"World Space Size: {size.x:0.#} x {size.y:0.#} (logical units; scale comes from the node Transform)");
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
            EditorGUI.TextWrapped("Draw Layer 0 excludes this Canvas from every camera. Use 1 for the default layer.");
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

/// <summary>布局编辑器：锚点、枢轴、Transform 位置与尺寸，预设修改合并为一次撤销。</summary>
[CustomEditor(typeof(UILayout))]
public sealed class UILayoutEditor : ComponentEditor
{
    private readonly UILayoutGizmos gizmos = new();
    private bool setPivot;
    private bool setPosition;

    /// <summary>绘制锚点预设与矩形参数。</summary>
    public override void OnDrawInspector()
    {
        if (Target.Ens.GetComponent<UILayout>() is not UILayout layout) return;
        DrawProperty("enabled");
        EditorGUI.Label("Anchor Presets");
        EditorGUI.Checkbox("Also Set Pivot", ref setPivot);
        EditorGUI.Checkbox("Also Set Position / Stretch Size", ref setPosition);
        if (!setPosition && UIWorldContext.Current?.FindNode(layout.EnsId)?.Parent?.Layout == null)
            EditorGUI.TextWrapped("Preserving the rectangle requires a parent UI layout in Scene preview. Enable Also Set Position to apply without it.");
        EditorGUI.BeginDisabled(Targets.Count != 1 || layout.HasDrivenRect);

        //绘制固定锚点与单轴、双轴拉伸预设
        string[] columns = ["Left", "Center", "Right", "Stretch"];
        string[] rows = ["Top", "Middle", "Bottom", "Stretch"];
        if (EditorGUI.BeginTable("anchors", 4, false))
        {
            for (int row = 0; row < 4; ++row)
            {
                EditorGUI.TableNextRow();
                for (int column = 0; column < 4; ++column)
                {
                    EditorGUI.TableSetColumnIndex(column);
                    vector2 min = new(column == 3 ? 0 : column * 0.5f, row == 3 ? 0 : 1 - row * 0.5f);
                    vector2 max = new(column == 3 ? 1 : min.x, row == 3 ? 1 : min.y);
                    bool selected = layout.GetAnchorMin().Equals(min) && layout.GetAnchorMax().Equals(max);
                    vector2 origin = EditorGUI.CursorScreenPosition;
                    if (EditorGUI.ToggleButton($"        \n        \n        ##{row}_{column}", selected))
                        ApplyPreset(layout, min, max);
                    //SetTooltip 自己不做悬停判断，不加这一句就是十六格每帧各建一条提示
                    //最后一条（Stretch / Stretch）会盖掉前面的，一直挂在鼠标上
                    if (NativeEditorGUI.IsItemHovered())
                        EditorGUI.SetTooltip($"{rows[row]} / {columns[column]}");
                    EditorGUI.DrawRectOutline(new vector2(origin.x + 9, origin.y + 8),
                        new vector2(origin.x + 39, origin.y + 38), new color(0.5f, 0.5f, 0.5f, 1));
                    EditorGUI.DrawRectOutline(new vector2(origin.x + 8 + min.x * 30, origin.y + 7 + (1 - max.y) * 30),
                        new vector2(origin.x + 10 + max.x * 30, origin.y + 9 + (1 - min.y) * 30), new color(1, 0.6f, 0.2f, 1), 2);
                }
            }
            EditorGUI.EndTable();
        }

        //按轴显示固定尺寸或拉伸边距
        vector2 offset = layout.GetOffset();
        vector2 size = layout.GetSizeDelta();
        vector2 pivot = layout.GetPivot();
        vector2 minAnchor = layout.GetAnchorMin();
        vector2 maxAnchor = layout.GetAnchorMax();
        bool stretchX = minAnchor.x != maxAnchor.x;
        bool stretchY = minAnchor.y != maxAnchor.y;
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
        if (Targets.Count != 1) EditorGUI.TextWrapped("Presets and derived dimensions require one selection. Raw fields below support multiple selections.");
        if (layout.HasDrivenRect) EditorGUI.TextWrapped("Rectangle is driven by a layout group or control.");
        EditorGUI.Separator();
        //绘制锚点、轴心与布局开关
        DrawProperty("anchorMin");
        DrawProperty("anchorMax");
        DrawProperty("pivot");
        DrawProperty("fitWidth");
        DrawProperty("fitHeight");
        DrawProperty("ignoreLayout");
        UIRect rect = layout.GetResolvedRect();
        EditorGUI.Label($"Resolved Size: {rect.Width:0.##} x {rect.Height:0.##}");
        EditorGUI.TextWrapped("Anchors / Pivot: (0,0) bottom-left, (1,1) top-right. Stretch margins are relative to the anchor edges. Fit axes use measured content size.");
    }

    /// <summary>绘制场景矩形手柄。</summary>
    public override void OnSceneGui()
    {
        if (Target.Ens.GetComponent<UILayout>() is UILayout layout) gizmos.OnSceneGui(layout);
    }

    //计算预设补偿并记录布局与 Transform 的一次撤销
    private void ApplyPreset(UILayout layout, vector2 min, vector2 max)
    {
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
