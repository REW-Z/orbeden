namespace Orbeden;

/// <summary>运行时 Immediate GUI API。</summary>
public static class GUI
{
    /// <summary>绘制文本标签。</summary>
    public static void Label(string text)
    {
        NativeGui.Label(text);
    }

    /// <summary>绘制按钮并返回本帧是否点击。</summary>
    public static bool Button(string text)
    {
        return NativeGui.Button(text);
    }

    /// <summary>开始一个浮动面板，调用后必须匹配 EndPanel。</summary>
    public static bool BeginPanel(string title)
    {
        return NativeGui.BeginPanel(title);
    }

    /// <summary>结束当前浮动面板。</summary>
    public static void EndPanel()
    {
        NativeGui.EndPanel();
    }
}
