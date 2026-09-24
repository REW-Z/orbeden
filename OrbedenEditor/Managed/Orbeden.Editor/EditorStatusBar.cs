using Orbeden;

namespace OrbedenEditor;

/// <summary>编辑器底部状态栏：显示 Print 的临时提示，没有提示时回退到 Console 最新一条。</summary>
public static class EditorStatusBar
{
    private static string printed = string.Empty;
    //Print 当时的日志最新序号：Console 再来新消息，提示就让位给那条消息
    private static long printedRevision = -1;
    //Console 回退文本按序号缓存，同一条消息不每帧拷贝
    private static long consoleRevision = -1;
    private static string consoleMessage = string.Empty;
    private static color consoleColor = default;

    /// <summary>打印一条临时提示；Console 出现新消息时它自动让位。</summary>
    public static void Print(string message)
    {
        printed = message ?? string.Empty;
        NativeEditorLog.GetRange(out _, out printedRevision);
        //空闲编辑器不出帧，提示落地要自己唤醒一帧
        EditorApplication.RequestRepaint();
    }

    //绘制状态栏内容。原生每帧在底边栏里调用，这里不请求重绘：状态随 Print 与日志写入而变
    internal static void Draw()
    {
        long newest = RefreshConsoleMessage();
        if (printed.Length != 0 && printedRevision == newest)
        {
            EditorGUI.Label(printed);
            return;
        }

        printed = string.Empty;
        if (consoleMessage.Length != 0) EditorGUI.TextColored(consoleMessage, consoleColor);
    }

    //读取 Console 最新一条并按序号缓存，返回当前最新序号
    private static long RefreshConsoleMessage()
    {
        int retained = NativeEditorLog.GetRange(out _, out long newest);
        if (newest == consoleRevision) return newest;

        consoleRevision = newest;
        consoleMessage = string.Empty;
        consoleColor = default;
        //序号区间是半开的，最新一条是 newest - 1；窗口为空或已过期时没有可显示的消息
        if (retained <= 0 || newest <= 0) return newest;
        string? message = NativeEditorLog.CopyEntry(newest - 1, out int level, out _);
        if (message == null) return newest;

        consoleMessage = message;
        consoleColor = LevelColor(level);
        return newest;
    }

    //读取级别配色，与 Console 面板对同一条日志的观感保持一致
    private static color LevelColor(int level)
    {
        EditorTheme theme = EditorTheme.Current;
        return level switch
        {
            EditorLogLevel.Warning => theme.LogWarning,
            EditorLogLevel.Error => theme.LogError,
            _ => theme.LogInfo,
        };
    }
}
