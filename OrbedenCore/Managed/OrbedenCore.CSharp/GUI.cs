namespace OrbedenCore.CSharp;

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

    /// <summary>开始绘制一个组件块。</summary>
    public static void BeginComponentBlock(string title)
    {
        NativeGui.BeginComponentBlock(title);
    }

    /// <summary>结束当前组件块。</summary>
    public static void EndComponentBlock()
    {
        NativeGui.EndComponentBlock();
    }

    /// <summary>绘制布尔输入框。</summary>
    public static bool Checkbox(string label, ref bool value)
    {
        return NativeGui.Checkbox(label, ref value);
    }

    /// <summary>绘制整数输入框。</summary>
    public static bool InputInt(string label, ref int value)
    {
        return NativeGui.InputInt(label, ref value);
    }

    /// <summary>绘制浮点输入框。</summary>
    public static bool InputFloat(string label, ref float value)
    {
        return NativeGui.InputFloat(label, ref value);
    }

    /// <summary>绘制三维向量输入框。</summary>
    public static bool InputVector3(string label, ref vector3 value)
    {
        return NativeGui.InputVector3(label, ref value);
    }

    /// <summary>绘制字符串输入框。</summary>
    public static bool InputText(string label, ref string value)
    {
        return NativeGui.InputText(label, ref value);
    }
}
