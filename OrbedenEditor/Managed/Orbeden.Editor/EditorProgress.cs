using Orbeden;

namespace OrbedenEditor;

/// <summary>进度的展示位：状态栏细条用于后台任务，模态浮窗用于长任务。</summary>
internal enum EditorProgressSurface
{
    StatusBar,
    Modal,
}

/// <summary>编辑器唯一的进度状态源。任务方只调 Begin/Report/End，两个展示位都读这里。
///
/// 生产者与消费者都在主线程：推进与绘制都在原生每帧的回调里，因此不加锁。
/// 空闲编辑器不出帧，所以任务在跑期间必须由任务方每帧请求重绘。</summary>
internal static class EditorProgress
{
    //弹窗 ID 固定，标题在前，ImGui 用 ### 之后的部分认窗口
    private const string ModalId = "Script Build###editor_progress_modal";
    private const float ModalWidth = 460.0f;
    private const float ModalBarHeight = 18.0f;
    private const float StatusBarBarHeight = 8.0f;
    //不确定进度扫一个来回的秒数
    private const float PulseSeconds = 1.6f;
    //状态栏里文案与进度条之间、进度条与时长之间的间距
    private const float StatusBarGap = 16.0f;
    private const float StatusBarBarMinWidth = 64.0f;

    private static readonly EditorRectPrimitive[] rects = new EditorRectPrimitive[4];
    private static readonly GuiContent title = new();
    private static string detail = string.Empty;
    private static readonly GuiContent statusLabel = new();
    private static readonly GuiContent clock = new();
    private static readonly GuiContent modalDetail = new();
    private static readonly GuiContent modalTitle = new(ModalId);
    private static readonly GuiContent barId = new("##editor_progress_bar");
    private static readonly GuiContent cancelLabel = new("Cancel##editor_progress_cancel");
    private static readonly GuiContent backgroundLabel = new("Run in Background##editor_progress_background");
    private static long displayedSeconds = -1;
    private static float ratio = -1.0f;
    private static EditorProgressSurface surface = EditorProgressSurface.StatusBar;
    private static bool active;
    private static bool canCancel;
    private static Action? onCancel;
    private static DateTime startedAt;
    private static bool modalOpenRequested;

    /// <summary>开始一个任务并设定展示位；进度重置为不确定态。</summary>
    internal static void Begin(string taskTitle, string taskDetail, EditorProgressSurface taskSurface,
        bool taskCanCancel, Action? cancelAction)
    {
        title.Text = taskTitle;
        detail = taskDetail;
        statusLabel.Text = title.Text + "  ·  " + detail;
        ratio = -1.0f;
        surface = taskSurface;
        canCancel = taskCanCancel;
        onCancel = cancelAction;
        startedAt = DateTime.UtcNow;
        displayedSeconds = -1;
        RefreshClockContent();
        active = true;
        //不在这里开弹窗：任务在同一帧内结束时不至于闪一下
        modalOpenRequested = taskSurface == EditorProgressSurface.Modal;
    }

    /// <summary>更新阶段文案；ratio 传非负数才转成确定进度，传负数保持不确定进度。</summary>
    internal static void Report(string taskDetail, float taskRatio = -1.0f)
    {
        if (detail != taskDetail)
        {
            detail = taskDetail;
            statusLabel.Text = title.Text + "  ·  " + detail;
            if (surface == EditorProgressSurface.Modal) modalDetail.Text = detail + "   " + clock.Text;
        }
        if (taskRatio >= 0.0f) ratio = Math.Clamp(taskRatio, 0.0f, 1.0f);
    }

    /// <summary>结束任务并收起进度；可重复调用。</summary>
    internal static void End()
    {
        active = false;
        modalOpenRequested = false;
        canCancel = false;
        onCancel = null;
    }

    /// <summary>有任务时整行画进度条并返回 true；没有任务时返回 false，让位给日志文本。</summary>
    internal static bool DrawStatusBarRow()
    {
        if (!active) return false;

        vector2 origin = NativeEditorGUI.GetCursorScreenPos();
        vector2 available = NativeEditorGUI.GetContentRegionAvail();
        if (available.x <= 0.0f || available.y <= 0.0f) return true;

        RefreshClockContent();
        //CalcButtonWidth 含按钮内边距，作为纯文本宽度要先减掉两侧内边距
        float padding = EditorTheme.Current.FramePaddingX * 2.0f;
        float labelWidth = Math.Max(NativeEditorGUI.CalcButtonWidth(statusLabel) - padding, 1.0f);
        float clockWidth = Math.Max(NativeEditorGUI.CalcButtonWidth(clock) - padding, 1.0f);

        //进度条占满文案与时长之间剩下的宽度，放不下就只显示文案
        float barLeft = origin.x + labelWidth + StatusBarGap;
        float barRight = origin.x + available.x - clockWidth - StatusBarGap;
        if (barRight - barLeft >= StatusBarBarMinWidth)
        {
            float barTop = origin.y + Math.Max((available.y - StatusBarBarHeight) * 0.5f, 0.0f);
            DrawBar(new vector2(barLeft, barTop), barRight - barLeft, StatusBarBarHeight);
            NativeEditorGUI.DrawTextClipped(origin, new vector2(origin.x + available.x, origin.y + available.y),
                new vector2(barRight + StatusBarGap, origin.y), EditorTheme.Current.Text, clock);
        }

        NativeEditorGUI.DrawTextClipped(origin, new vector2(origin.x + labelWidth, origin.y + available.y),
            origin, EditorTheme.Current.Text, statusLabel);
        return true;
    }

    /// <summary>绘制模态进度浮窗；原生在顶层窗口上下文每帧调用一次。</summary>
    internal static void DrawModal()
    {
        if (modalOpenRequested)
        {
            NativeEditorGUI.OpenPopup(ModalId);
            modalOpenRequested = false;
        }

        if (!NativeEditorGUI.BeginDialog(modalTitle, ModalWidth)) return;

        try
        {
            //任务结束或展示位被切走时，只能在弹窗自己的 Begin/End 之间把它收起来
            if (!active || surface != EditorProgressSurface.Modal)
            {
                NativeEditorGUI.ClosePopup();
                return;
            }
            DrawModalBody();
        }
        finally { EditorGUI.EndPopup(); }
    }

    //绘制模态正文：标题、阶段文案与时长、进度条、取消与转后台
    private static void DrawModalBody()
    {
        RefreshClockContent();
        EditorGUI.Label(title);
        EditorGUI.Label(modalDetail);
        EditorGUI.Separator();

        vector2 origin = NativeEditorGUI.GetCursorScreenPos();
        vector2 available = NativeEditorGUI.GetContentRegionAvail();
        float width = Math.Max(available.x, 1.0f);
        NativeEditorGUI.InvisibleButton(barId, new vector2(width, ModalBarHeight));
        DrawBar(origin, width, ModalBarHeight);

        EditorGUI.Separator();
        if (canCancel && EditorGUI.Button(cancelLabel))
        {
            //先收起弹窗再执行动作，动作里再弹窗也不会被自己顶掉
            Action? action = onCancel;
            NativeEditorGUI.ClosePopup();
            action?.Invoke();
            return;
        }
        if (canCancel) EditorGUI.SameLine();
        if (EditorGUI.Button(backgroundLabel))
        {
            surface = EditorProgressSurface.StatusBar;
            NativeEditorGUI.ClosePopup();
        }
    }

    //把进度条画到指定位置：底色、填充、顶面描边各一个矩形
    private static void DrawBar(vector2 origin, float width, float height)
    {
        float filled = (ratio >= 0.0f ? Math.Clamp(ratio, 0.0f, 1.0f) : GetPulse()) * width;
        int count = 0;
        count = EditorRects.Append(rects, count, origin.x, origin.y, origin.x + width, origin.y + height,
            EditorTheme.Current.Control);
        //填充不足半个像素就不画：亚像素矩形会渲染成断断续续的虚线
        if (filled >= 0.5f)
        {
            count = EditorRects.Append(rects, count, origin.x, origin.y, origin.x + filled, origin.y + height,
                EditorTheme.Current.Active);
        }
        count = EditorRects.Append(rects, count, origin.x, origin.y, origin.x + width, origin.y + 1.0f,
            EditorTheme.Current.Border);
        NativeEditorGUI.DrawRects(rects, count);
    }

    //不确定进度的脉冲相位：按墙上时钟推进，空闲唤醒的那些帧不会跳变
    private static float GetPulse()
    {
        float phase = (float)((DateTime.UtcNow - startedAt).TotalSeconds / PulseSeconds);
        float t = phase - MathF.Floor(phase);
        return 0.15f + 0.7f * (t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f);
    }

    //更新变化后的显示秒数与模态文案
    private static void RefreshClockContent()
    {
        TimeSpan elapsed = DateTime.UtcNow - startedAt;
        long seconds = (long)elapsed.TotalSeconds;
        if (seconds == displayedSeconds) return;
        displayedSeconds = seconds;
        clock.Text = $"{(int)elapsed.TotalMinutes:00}:{elapsed.Seconds:00}";
        if (surface == EditorProgressSurface.Modal) modalDetail.Text = detail + "   " + clock.Text;
    }
}
