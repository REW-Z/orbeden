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

    /// <summary>开始一个可折叠、可选移除的组件块。</summary>
    public static bool BeginCollapsibleComponentBlock(string title, string id, bool removable, out bool removeRequested)
    {
        return NativeGui.BeginCollapsibleComponentBlock(title, id, removable, out removeRequested);
    }

    /// <summary>开始一个下拉选择框，返回本帧是否展开。</summary>
    public static bool BeginCombo(string label, string preview)
    {
        return NativeGui.BeginCombo(label, preview);
    }

    /// <summary>结束当前下拉选择框。</summary>
    public static void EndCombo()
    {
        NativeGui.EndCombo();
    }

    /// <summary>绘制下拉选择项并返回本帧是否点击。</summary>
    public static bool Selectable(string label, bool selected = false)
    {
        return NativeGui.Selectable(label, selected);
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
