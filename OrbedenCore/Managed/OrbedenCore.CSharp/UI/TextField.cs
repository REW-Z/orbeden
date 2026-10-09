using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 单行文本输入框。编辑模型在 UITextEditor 里，这里负责输入路由、输入法会话与绘制：
/// 背景→选区→文本/组合→光标，附加网格由一层内裁剪约束在矩形内。
/// </summary>
public class TextField : UIControl
{
    /// <summary>光标闪烁周期，秒；前半个周期可见。</summary>
    public const float CaretBlinkPeriod = 1.0f;

    /// <summary>光标与边界之间至少保留的逻辑单位数。</summary>
    public const float CaretEdgeMargin = 1.0f;

    [SerializeField] private string text = string.Empty;
    [SerializeField] private Font? font;
    [SerializeField] private float fontSize = 16.0f;
    [SerializeField] private int maxLength;
    [SerializeField] private bool readOnly;
    [SerializeField] private string placeholder = string.Empty;
    [SerializeField] private color textTint = new(1.0f, 1.0f, 1.0f, 1.0f);
    [SerializeField] private color selectionTint = new(0.2f, 0.4f, 1.0f, 0.5f);
    [SerializeField] private color caretTint = new(1.0f, 1.0f, 1.0f, 1.0f);

    /// <summary>文本变化。派发时触发；同值不通知。</summary>
    public event Action<string>? TextChanged;

    /// <summary>提交（回车）。派发时触发。</summary>
    public event Action<string>? Submitted;

    private readonly UITextEditor editor = new();
    private readonly UITextLayout layoutBuilder = new(FontAtlasCache.Shared);
    private TextLayoutResult? layout;
    private string layoutText = string.Empty;
    private float layoutFontSize;
    private FontRasterMode layoutRasterMode;
    //本次绘制用的字形光栅比例与上次排版的记录；两者不同就要重排。
    private float rasterScale = 1.0f;
    private float layoutRasterScale = 1.0f;
    private Font? layoutFont;
    private ulong layoutFontRevision;
    private ulong layoutAtlasRevision;
    private TextLayoutResult? placeholderLayout;
    private TextLayoutResult? compositionLayout;
    private AuxiliaryLayoutKey placeholderLayoutKey;
    private AuxiliaryLayoutKey compositionLayoutKey;
    private readonly record struct AuxiliaryLayoutKey(string Text, Font? Font, ulong FontRevision, ulong AtlasRevision,
        float FontSize, FontRasterMode RasterMode, float RasterScale);
    private readonly List<(UIGlyphEntry entry, uint generation)> glyphPages = [];
    private bool overlayGlyphsNeedRetry;

    //视图状态：光标闪烁计时与横向滚动。
    private float caretTimer;
    private float scrollX;

    /// <summary>创建输入框组件包装。</summary>
    public TextField(Ens ens) : base(ens)
    {
        editor.TextChanged += OnEditorTextChanged;
        editor.MaxLength = maxLength;
        editor.ReadOnly = readOnly;
        editor.SetText(text);
    }

    /// <summary>当前文本。</summary>
    public string GetText() => editor.GetText();

    /// <summary>设置文本：走与输入相同的归一流程，按最大长度截断，同值不通知。</summary>
    public void SetText(string value, bool notify = true)
    {
        string before = editor.GetText();
        editor.SetText(value);
        if (editor.GetText() == before) return;
        text = editor.GetText();
        if (notify) RaiseEvent(UIEventIds.TextChanged, UIEventPayload.Text(text));
    }

    /// <summary>显式字体；空表示使用内置默认字体。</summary>
    public Font? GetFont() => font;

    /// <summary>获取实际排版字体；未指定时使用内置默认字体。</summary>
    public Font? GetEffectiveFont()
    {
        if (font is { IsAlive: true }) return font;
        if (!ReferenceEquals(font, null)
            && Script.ResolveReference(font.ResourceKey, typeof(Font)) is Font restored)
        {
            font = restored;
            return restored;
        }
        return UIWorldContext.Current?.GetDefaultFont();
    }

    /// <summary>设置字体。</summary>
    public void SetFont(Font? value)
    {
        if (ReferenceEquals(font, value)) return;
        font = value;
        layout = null;
        SetOverlayDirty();
    }

    /// <summary>字号。</summary>
    public float GetFontSize() => fontSize;

    /// <summary>设置字号；小于 1 夹紧到 1。</summary>
    public void SetFontSize(float value)
    {
        float next = float.IsFinite(value) && value > 1.0f ? value : 1.0f;
        if (fontSize == next) return;
        fontSize = next;
        layout = null;
        SetOverlayDirty();
    }

    /// <summary>读取字体资源的光栅化模式。</summary>
    public FontRasterMode GetRasterMode() => GetEffectiveFont()?.rasterMode ?? FontRasterMode.Bitmap;

    /// <summary>最大长度，按标量计数；0 为不限制。</summary>
    public int GetMaxLength() => maxLength;

    /// <summary>设置最大长度；负值按 0 处理，超长时立即截断当前文本。</summary>
    public void SetMaxLength(int value)
    {
        int next = Math.Max(0, value);
        if (maxLength == next) return;
        maxLength = next;
        editor.MaxLength = next;
        if (next > 0) editor.SetText(UITextEditor.Truncate(editor.GetText(), next));
        SetOverlayDirty();
    }

    /// <summary>是否只读；只读仍可选择与复制。</summary>
    public bool GetReadOnly() => readOnly;

    /// <summary>设置只读。</summary>
    public void SetReadOnly(bool value)
    {
        if (readOnly == value) return;
        readOnly = value;
        editor.ReadOnly = value;
    }

    /// <summary>占位文本；仅在实际文本为空且没有组合时显示。</summary>
    public string GetPlaceholder() => placeholder;

    /// <summary>设置占位文本。</summary>
    public void SetPlaceholder(string value)
    {
        string next = value ?? string.Empty;
        if (placeholder == next) return;
        placeholder = next;
        SetOverlayDirty();
    }

    /// <summary>状态色：文本、选区与光标。</summary>
    public void SetColors(color textColor, color selectionColor, color caretColor)
    {
        textTint = textColor;
        selectionTint = selectionColor;
        caretTint = caretColor;
        SetOverlayDirty();
    }

    /// <summary>光标位置（标量下标）。</summary>
    public int GetCaret() => editor.GetCaret();

    /// <summary>选区起止（标量下标）。</summary>
    public void GetSelection(out int start, out int end)
    {
        start = editor.GetSelectionStart();
        end = editor.GetSelectionEnd();
    }

    /// <summary>设置选区；越界夹紧，顺序自动理顺。</summary>
    public void SetSelection(int start, int end)
    {
        editor.SetSelection(start, end);
        ResetCaretBlink();
        SetOverlayDirty();
    }

    /// <summary>移动光标；select 为真时保留锚点形成选区。</summary>
    public void MoveCaret(int delta, bool select)
    {
        editor.MoveCaret(delta, select);
        ResetCaretBlink();
        SetOverlayDirty();
    }

    /// <summary>是否处于组合状态。</summary>
    public bool HasComposition() => editor.HasComposition;

    /// <summary>开始输入法组合。</summary>
    public void BeginComposition(ulong token)
    {
        editor.BeginComposition(token);
        SetOverlayDirty();
    }

    /// <summary>更新组合内容。</summary>
    public void UpdateComposition(ulong token, string value, int caret)
    {
        editor.UpdateComposition(token, value, caret);
        SetOverlayDirty();
    }

    /// <summary>提交组合；成功后令牌作废，重复提交被忽略。</summary>
    public bool CommitComposition(ulong token, string value)
    {
        bool committed = editor.CommitComposition(token, value);
        if (committed) ResetCaretBlink();
        SetOverlayDirty();
        return committed;
    }

    /// <summary>取消组合：不改变实际文本。</summary>
    public void CancelComposition()
    {
        editor.CancelComposition();
        SetOverlayDirty();
    }

    /// <summary>用给定文本替换当前选区。</summary>
    public bool ReplaceSelection(string value)
    {
        bool edited = editor.ReplaceSelection(value);
        if (edited) ResetCaretBlink();
        SetOverlayDirty();
        return edited;
    }

    /// <summary>删除光标前一个标量，或整个选区。</summary>
    public bool DeleteBackward()
    {
        bool edited = editor.DeleteBackward();
        if (edited) ResetCaretBlink();
        SetOverlayDirty();
        return edited;
    }

    /// <summary>删除光标后一个标量，或整个选区。</summary>
    public bool DeleteForward()
    {
        bool edited = editor.DeleteForward();
        if (edited) ResetCaretBlink();
        SetOverlayDirty();
        return edited;
    }

    /// <summary>全选。</summary>
    public void SelectAll()
    {
        editor.SelectAll();
        ResetCaretBlink();
        SetOverlayDirty();
    }

    /// <summary>复制选区到剪贴板；没有选区或写入失败时返回假。</summary>
    public bool CopySelection()
    {
        string selected = editor.GetSelectedText();
        if (selected.Length == 0) return false;
        return WriteClipboard(selected);
    }

    /// <summary>剪切选区：只读时拒绝，其余与复制一致再删除。</summary>
    public bool CutSelection()
    {
        if (readOnly) return false;
        if (!CopySelection()) return false;
        return ReplaceSelection(string.Empty);
    }

    /// <summary>粘贴：文本按输入归一流程处理，只读时拒绝。</summary>
    public bool Paste()
    {
        if (readOnly) return false;
        if (!ReadClipboard(out string clipboard) || clipboard.Length == 0) return false;
        return ReplaceSelection(clipboard);
    }

    /// <summary>指针按下：取得焦点并把光标放到点击位置。</summary>
    public override void OnPointerDown(in UIPointerEvent input)
    {
        if (!CanInteract()) return;
        Focus();
        ResetCaretBlink();
        //先用已有排版结果定位；没有排版结果时只取焦点。
        if (layout != null && TryToLocal(input.position, out vector2 local))
        {
            int caret = HitCaret(local);
            editor.SetCaret(caret, select: false);
        }
        SyncTextInput();
        SetOverlayDirty();
    }

    /// <summary>指针抬起：不做额外处理，输入框靠焦点工作。</summary>
    public override void OnPointerUp(in UIPointerEvent input)
    {
    }

    /// <summary>获得焦点：开启平台文本输入。</summary>
    public override void OnFocusChanged(bool focused)
    {
        if (focused) SyncTextInput();
        else ReleaseTextInput();
    }

    /// <summary>控件停用：收起输入法并交出焦点。</summary>
    protected override void OnUIDisabled()
    {
        ReleaseTextInput();
        UIWorldContext.Current?.InputRouter.CancelNode(EnsId);
    }

    /// <summary>控件摘除：释放文本输入会话。</summary>
    protected override void OnUIDetached()
    {
        ReleaseTextInput();
        UIWorldContext.Current?.InputRouter.CancelNode(EnsId);
    }

    /// <summary>每帧推进光标闪烁；只有持有焦点时才走。</summary>
    internal void TickCaret(float deltaTime)
    {
        if (!HasFocus() || !float.IsFinite(deltaTime) || deltaTime <= 0.0f) return;
        caretTimer += deltaTime;
        //整周期取模，长时间运行也不会溢出。
        if (caretTimer >= CaretBlinkPeriod) caretTimer %= CaretBlinkPeriod;
        SetOverlayDirty();
    }

    /// <summary>输入框永远有附加内容：选区、文本与光标。</summary>
    protected override bool HasOverlay => true;

    /// <summary>检查字体重导入与图集页回收。</summary>
    protected override bool IsOverlayInvalidated()
    {
        Font? effectiveFont = GetEffectiveFont();
        bool changed = !ReferenceEquals(layoutFont, effectiveFont)
            || layoutFontRevision != (effectiveFont?.GetRevision() ?? 0)
            || layoutAtlasRevision != FontAtlasCache.Shared.Revision
            || overlayGlyphsNeedRetry;
        if (layout != null)
            foreach (UITextGlyph glyph in layout.glyphs) changed |= glyph.entry.NeedsRetry;
        foreach ((UIGlyphEntry entry, uint generation) in glyphPages)
        {
            changed |= entry.PageGeneration != generation;
            if (entry.PageGeneration == generation && IsUIActive()) FontAtlasCache.Shared.PinGlyph(entry);
        }
        if (changed) layout = null;
        return changed;
    }

    /// <summary>绘制：背景由同节点的图形负责，这里画选区、文本、组合与光标。</summary>
    protected override void PopulateOverlay(UIMeshBuilder mesh)
    {
        glyphPages.Clear();
        overlayGlyphsNeedRetry = false;
        UIRect rect = GetLayout()?.GetResolvedRect() ?? default;
        if (rect.Width <= 0.0f || rect.Height <= 0.0f) return;
        //所有内部图形都被这层矩形裁剪约束。
        mesh.SetTexture(null, UIMaterialKind.ImageStraight);
        //字形位图尺寸跟着视图走，与同节点的 Text 用同一套比例。
        rasterScale = mesh.ViewScale;

        TextLayoutResult? current = EnsureLayout();
        if (current == null) return;

        var (origin, visible) = ResolveContentOrigin(rect, current);
        DrawSelection(mesh, current, origin, visible);
        DrawText(mesh, current, origin, visible);
        DrawCaret(mesh, current, origin, visible);
    }

    //排版结果按文本、字体、字号、模式与光栅缩放缓存；任一变化才重排。
    private TextLayoutResult? EnsureLayout()
    {
        string source = editor.GetText();
        Font? effectiveFont = GetEffectiveFont();
        FontRasterMode rasterMode = GetRasterMode();
        ulong revision = effectiveFont?.GetRevision() ?? 0;
        if (layout != null && layoutText == source && layoutFontSize == fontSize
            && layoutRasterMode == rasterMode && layoutRasterScale == rasterScale
            && ReferenceEquals(layoutFont, effectiveFont) && layoutFontRevision == revision
            && layoutAtlasRevision == FontAtlasCache.Shared.Revision)
        {
            return layout;
        }

        layout = layoutBuilder.Layout(source, effectiveFont, fontSize, 0.0f, wrap: false, lineSpacing: 1.0f,
            rasterMode, UITextAlignment.Left, rasterScale);
        layoutText = source;
        layoutFontSize = fontSize;
        layoutRasterMode = rasterMode;
        layoutRasterScale = rasterScale;
        layoutFont = effectiveFont;
        layoutFontRevision = revision;
        layoutAtlasRevision = FontAtlasCache.Shared.Revision;
        return layout;
    }

    //内容原点：垂直居中，水平按滚动量左移。
    private (vector2 origin, UIRect visible) ResolveContentOrigin(UIRect rect, TextLayoutResult current)
    {
        float baseline = rect.min.y + (rect.Height - current.ascender) * 0.5f;
        vector2 origin = new(rect.min.x + CaretEdgeMargin - scrollX, baseline);
        UIRect visible = new(new vector2(rect.min.x + CaretEdgeMargin, rect.min.y),
            new vector2(MathF.Max(rect.Width - CaretEdgeMargin * 2.0f, 0.0f), rect.Height));
        return (origin, visible);
    }

    //选区：按标量范围取矩形；被可见区域裁剪。
    private void DrawSelection(UIMeshBuilder mesh, TextLayoutResult current, vector2 origin, UIRect visible)
    {
        if (!editor.HasSelection()) return;
        int start = editor.GetSelectionStart();
        int end = editor.GetSelectionEnd();

        for (int lineIndex = 0; lineIndex < current.lines.Length; ++lineIndex)
        {
            UITextLine line = current.lines[lineIndex];
            if (end <= line.firstScalar || start >= line.EndScalar) continue;

            int localStart = Math.Max(start, line.firstScalar);
            int localEnd = Math.Min(end, line.EndScalar);
            float x0 = origin.x + OffsetInLine(current, line, localStart);
            float x1 = origin.x + OffsetInLine(current, line, localEnd);
            float top = origin.y + line.baseline - current.ascender * 0.0f;
            UIRect band = new(new vector2(x0, origin.y - current.lineAdvance * 0.2f),
                new vector2(MathF.Max(x1 - x0, 0.0f), current.lineAdvance));
            if (!TryClip(band, visible, out UIRect clipped)) continue;
            _ = top;
            mesh.AddQuad(clipped, new vector2(0.0f, 0.0f), new vector2(1.0f, 1.0f), selectionTint);
        }
    }

    //文本或占位文本：逐字形提交，按页分组交给字形缓存。
    private void DrawText(UIMeshBuilder mesh, TextLayoutResult current, vector2 origin, UIRect visible)
    {
        bool showPlaceholder = editor.GetText().Length == 0 && !editor.HasComposition;
        color tint = textTint;

        if (showPlaceholder && placeholder.Length != 0)
        {
            TextLayoutResult displayed = GetAuxiliaryLayout(placeholder, ref placeholderLayout, ref placeholderLayoutKey);
            DrawGlyphs(mesh, displayed, origin, visible, tint with { a = tint.a * 0.5f });
            return;
        }

        DrawGlyphs(mesh, current, origin, visible, tint);

        //组合文本画在光标处，用下划线区分：这里只画文字，样式由上层决定。
        if (editor.HasComposition && editor.GetCompositionText().Length != 0)
        {
            TextLayoutResult composition = GetAuxiliaryLayout(editor.GetCompositionText(), ref compositionLayout, ref compositionLayoutKey);
            vector2 caretOrigin = new(origin.x + CaretOffset(current), origin.y);
            DrawGlyphs(mesh, composition, caretOrigin, visible, tint);
        }
    }

    //复用占位文字和组合文字的排版并检查图集存活
    private TextLayoutResult GetAuxiliaryLayout(string source, ref TextLayoutResult? cached, ref AuxiliaryLayoutKey cachedKey)
    {
        Font? effectiveFont = GetEffectiveFont();
        AuxiliaryLayoutKey key = new(source, effectiveFont, effectiveFont?.GetRevision() ?? 0, FontAtlasCache.Shared.Revision,
            fontSize, GetRasterMode(), rasterScale);
        bool valid = cached != null && cachedKey == key;
        if (valid)
        {
            foreach (UITextGlyph glyph in cached!.glyphs)
            {
                UIGlyphEntry entry = glyph.entry;
                if (entry.NeedsRetry || (entry.page != null
                    && (!FontAtlasCache.Shared.TryGetGlyph(entry.Key, out UIGlyphEntry resident) || !ReferenceEquals(entry, resident))))
                { valid = false; break; }
            }
        }
        if (valid) return cached!;
        cached = layoutBuilder.Layout(source, effectiveFont, fontSize, 0.0f, false, 1.0f,
            key.RasterMode, UITextAlignment.Left, rasterScale);
        cachedKey = key;
        return cached;
    }

    //提交字形的图集与裁剪区域
    private void DrawGlyphs(UIMeshBuilder mesh, TextLayoutResult current, vector2 origin, UIRect visible, color tint)
    {
        FontRasterMode rasterMode = GetRasterMode();
        UIMaterialKind kind = rasterMode switch
        {
            FontRasterMode.SDF => UIMaterialKind.SDF,
            FontRasterMode.MSDF => UIMaterialKind.MSDF,
            _ => UIMaterialKind.Bitmap,
        };
        Texture2D? lastTexture = null;
        bool started = false;
        foreach (UITextGlyph glyph in current.glyphs)
        {
            overlayGlyphsNeedRetry |= glyph.entry.NeedsRetry;
            if (!glyph.entry.HasPixels) continue;
            glyphPages.Add((glyph.entry, glyph.entry.PageGeneration));
            vector2 pen = new(origin.x + glyph.position.x, origin.y + glyph.position.y);
            if (!glyph.entry.TryGetTargetRect(pen, current.fontSize, out UIRect target)) continue;
            if (!TryClip(target, visible, out UIRect clipped)) continue;

            //按字形图集切换绘制片段
            if (!started || !ReferenceEquals(lastTexture, glyph.entry.Texture))
            {
                mesh.SetTexture(glyph.entry.Texture, kind);
                if (rasterMode != FontRasterMode.Bitmap) mesh.SetDistanceRange(GetEffectiveFont()?.distanceFieldRange ?? FontAtlasCache.DistanceFieldRange);
                lastTexture = glyph.entry.Texture;
                started = true;
            }

            //裁剪会改变 UV 范围：按裁剪比例换算。
            UIRect uv = glyph.entry.Uv;
            float u0 = uv.min.x + (clipped.min.x - target.min.x) / MathF.Max(target.Width, 1e-4f) * uv.Width;
            float u1 = uv.min.x + (clipped.Max.x - target.min.x) / MathF.Max(target.Width, 1e-4f) * uv.Width;
            float y0 = uv.min.y + (clipped.min.y - target.min.y) / MathF.Max(target.Height, 1e-4f) * uv.Height;
            float y1 = uv.min.y + (clipped.Max.y - target.min.y) / MathF.Max(target.Height, 1e-4f) * uv.Height;
            bool flipY = glyph.entry.Texture != null && !glyph.entry.Texture.IsRenderTarget();
            mesh.AddQuad(clipped, UIMeshBuilder.ToTextureUv(new vector2(u0, y0), flipY),
                    UIMeshBuilder.ToTextureUv(new vector2(u1, y1), flipY), tint);
        }
        if (started) mesh.SetTexture(null, UIMaterialKind.ImageStraight);
    }

    //光标：前半个周期可见，其余时间不画。
    private void DrawCaret(UIMeshBuilder mesh, TextLayoutResult current, vector2 origin, UIRect visible)
    {
        if (!HasFocus()) return;
        if (caretTimer >= CaretBlinkPeriod * 0.5f) return;

        float x = origin.x + CaretOffset(current);
        float height = current.lineAdvance;
        UIRect caret = new(new vector2(x, origin.y), new vector2(MathF.Max(fontSize * 0.05f, 1.0f), height));
        if (!TryClip(caret, visible, out UIRect clipped)) return;
        mesh.AddQuad(clipped, new vector2(0.0f, 0.0f), new vector2(1.0f, 1.0f), caretTint);
    }

    //光标（或组合光标）在排版结果中的水平位置。
    private float CaretOffset(TextLayoutResult current)
    {
        int scalar = editor.HasComposition
            ? editor.GetCompositionCaret() + editor.GetSelectionStart()
            : editor.GetCaret();
        vector2 position = current.GetCaretPosition(scalar);
        return position.x;
    }

    //某个标量边界在本行内的水平偏移。
    private static float OffsetInLine(TextLayoutResult current, in UITextLine line, int scalar)
    {
        float x = 0.0f;
        for (int offset = 0; offset < line.glyphCount; ++offset)
        {
            UITextGlyph glyph = current.glyphs[line.firstGlyph + offset];
            if (glyph.scalarIndex >= scalar) break;
            x = glyph.position.x + glyph.advance;
        }
        return x;
    }

    //把矩形夹到可见区域内；完全在外时返回假。
    private static bool TryClip(UIRect rect, UIRect visible, out UIRect clipped)
    {
        clipped = default;
        float minX = MathF.Max(rect.min.x, visible.min.x);
        float maxX = MathF.Min(rect.Max.x, visible.Max.x);
        float minY = MathF.Max(rect.min.y, visible.min.y);
        float maxY = MathF.Min(rect.Max.y, visible.Max.y);
        if (maxX <= minX || maxY <= minY) return false;
        clipped = new UIRect(new vector2(minX, minY), new vector2(maxX - minX, maxY - minY));
        return true;
    }

    //指针局部坐标换算成标量位置。
    private int HitCaret(vector2 local)
    {
        if (layout == null) return editor.GetCaret();
        UIRect rect = GetLayout()?.GetResolvedRect() ?? default;
        var (origin, _) = ResolveContentOrigin(rect, layout);
        return layout.HitTestCaret(new vector2(local.x - origin.x, local.y - origin.y));
    }

    private bool TryToLocal(vector2 windowPoint, out vector2 local)
    {
        local = default;
        UINode? node = UIWorldContext.Current?.FindNode(EnsId);
        if (node == null) return false;
        return UICanvasSpace.TryGetCanvasView(node, out UIView view)
            && UICanvasSpace.TryWindowToLocal(node, view, windowPoint, out local);
    }

    //把文本焦点交给平台输入法：会话标识用于丢弃离焦后的旧提交。
    private void SyncTextInput()
    {
        UIRect rect = GetLayout()?.GetResolvedRect() ?? default;
        UIWorldContext.Current?.SetTextInputFocus(this, token: unchecked((ulong)(uint)InstanceId),
            (int)rect.min.x, (int)rect.min.y, (int)rect.Width, (int)rect.Height);
    }

    private void ReleaseTextInput() => UIWorldContext.Current?.ClearTextInputFocus(this);

    //剪贴板经由桥接层，读写失败都不改变文本。
    private static bool ReadClipboard(out string value)
    {
        value = string.Empty;
        RetainedGuiBridge? bridge = UIWorldContext.Current?.NativeBridge;
        if (bridge == null) return false;

        int required = bridge.ReadClipboard([]);
        if (required <= 0) return false;
        byte[] buffer = new byte[required];
        int written = bridge.ReadClipboard(buffer);
        if (written <= 0 || written > buffer.Length) return false;
        value = InteropText.DecodeUtf8(buffer, 0, written);
        return true;
    }

    private static bool WriteClipboard(string value)
    {
        RetainedGuiBridge? bridge = UIWorldContext.Current?.NativeBridge;
        if (bridge == null) return false;
        return bridge.WriteClipboard(InteropText.EncodeUtf8(value));
    }

    private void ResetCaretBlink()
    {
        caretTimer = 0.0f;
        SetOverlayDirty();
    }

    private void OnEditorTextChanged(string value)
    {
        text = value;
        layout = null;
        RaiseEvent(UIEventIds.TextChanged, UIEventPayload.Text(value));
        SetOverlayDirty();
    }

    /// <summary>代码事件在派发时触发。</summary>
    protected override void RaiseCodeEvent(int eventId, in UIEventPayload payload)
    {
        if (eventId == UIEventIds.TextChanged) TextChanged?.Invoke(payload.text ?? string.Empty);
        else if (eventId == UIEventIds.Submitted) Submitted?.Invoke(payload.text ?? string.Empty);
    }

    /// <summary>提交一次：非组合状态下才发 Submitted。</summary>
    internal void RaiseSubmitted()
    {
        if (editor.HasComposition) return;
        RaiseEvent(UIEventIds.Submitted, UIEventPayload.Text(editor.GetText()));
    }

    /// <summary>编辑模型；输入模块与输入法通过它改文本。</summary>
    internal UITextEditor Editor => editor;
}
