using System;

namespace OrbedenEditor;

/// <summary>
/// 统一的模态确认弹窗。一次只挂一个请求，由发起它的面板每帧调用 Draw 绘制：
/// 模态窗会挡住输入，面板在被挡住期间不会隐藏，所以由发起者自己当宿主是安全的。
/// </summary>
internal static class EditorDialog
{
    //弹窗 ID 固定，标题在前，ImGui 用 ### 之后的部分认窗口
    private const string DialogId = "editor_dialog";
    //默认宽度：够放下一整行提示，又不至于横跨整个编辑器
    private const float DefaultWidth = 420.0f;

    private static string hostPanelId = string.Empty;
    private static string label = string.Empty;
    private static string message = string.Empty;
    private static string confirmLabel = "OK";
    private static float width = DefaultWidth;
    private static Action? onConfirm;
    private static bool openRequested;
    private static bool open;

    /// <summary>弹窗是否正在显示；显示期间全局命令应当让路。</summary>
    public static bool IsOpen => open;

    /// <summary>
    /// 请求一个确认弹窗，确认后执行 onConfirm。title 显示在标题栏，
    /// hostPanelId 对应的面板负责每帧绘制它。
    /// </summary>
    public static void Confirm(string hostPanelId, string title, string message, string confirmLabel, Action onConfirm,
        float width = DefaultWidth)
    {
        ArgumentNullException.ThrowIfNull(onConfirm);
        EditorDialog.hostPanelId = hostPanelId;
        EditorDialog.label = title + "###" + DialogId;
        EditorDialog.message = message;
        EditorDialog.confirmLabel = confirmLabel;
        EditorDialog.width = width;
        EditorDialog.onConfirm = onConfirm;
        openRequested = true;
        EditorApplication.RequestRepaint();
    }

    /// <summary>由 hostPanelId 对应的面板每帧调用；其它面板调用无效果。</summary>
    public static void Draw(string panelId)
    {
        if (!string.Equals(hostPanelId, panelId, StringComparison.Ordinal)) return;

        if (openRequested)
        {
            NativeEditorGUI.OpenPopup(label);
            openRequested = false;
        }
        open = NativeEditorGUI.BeginDialog(label, width);
        if (!open) return;

        //不必在这里请求重绘：模态暗化层是逐帧淡入的，出帧由 EditorSystem 的「模态框保持连续帧」统一保证
        try { DrawBody(); }
        finally { EditorGUI.EndPopup(); }
    }

    //绘制正文与按钮：确认先收起弹窗再执行动作，动作里再弹窗也不会被自己顶掉
    private static void DrawBody()
    {
        EditorGUI.Label(message);
        EditorGUI.Separator();
        if (EditorGUI.Button(confirmLabel + "##editor_dialog_confirm"))
        {
            Action? action = onConfirm;
            ClosePopup();
            action?.Invoke();
            return;
        }
        EditorGUI.SameLine();
        if (EditorGUI.Button("Cancel##editor_dialog_cancel")) ClosePopup();
    }

    //收起弹窗并清掉请求
    private static void ClosePopup()
    {
        hostPanelId = string.Empty;
        label = string.Empty;
        message = string.Empty;
        onConfirm = null;
        //收起后宿主不再进入 Draw，open 必须在这里落下来
        open = false;
        NativeEditorGUI.ClosePopup();
    }
}
