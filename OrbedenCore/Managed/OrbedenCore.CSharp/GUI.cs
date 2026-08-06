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

    /// <summary>绘制分隔线。</summary>
    public static void Separator() => NativeGui.Separator();

    /// <summary>让下一个控件与前一个控件同行。</summary>
    public static void SameLine() => NativeGui.SameLine();

    /// <summary>开始一个可滚动表格。</summary>
    public static bool BeginTable(string id, int columns) => NativeGui.BeginTable(id, columns);

    /// <summary>结束当前表格。</summary>
    public static void EndTable() => NativeGui.EndTable();

    /// <summary>配置一个表格列。</summary>
    public static void TableSetupColumn(string label, float width = 0.0f, bool fixedWidth = false)
        => NativeGui.TableSetupColumn(label, width, fixedWidth);

    /// <summary>绘制当前表格表头。</summary>
    public static void TableHeadersRow() => NativeGui.TableHeadersRow();

    /// <summary>前进到下一表格行。</summary>
    public static void TableNextRow() => NativeGui.TableNextRow();

    /// <summary>切换当前表格列。</summary>
    public static void TableSetColumnIndex(int column) => NativeGui.TableSetColumnIndex(column);

    /// <summary>绘制支持跨表格列的选择项。</summary>
    public static bool TableSelectable(string label, bool selected = false, bool spanAllColumns = true)
        => NativeGui.TableSelectable(label, selected, spanAllColumns);

    /// <summary>判断刚绘制的控件是否被左键双击。</summary>
    public static bool IsItemDoubleClicked() => NativeGui.IsItemDoubleClicked();

    /// <summary>开始刚绘制控件的右键菜单。</summary>
    public static bool BeginPopupContextItem(string id) => NativeGui.BeginPopupContextItem(id);

    /// <summary>开始当前窗口空白区域的右键菜单。</summary>
    public static bool BeginPopupContextWindow(string id) => NativeGui.BeginPopupContextWindow(id);

    /// <summary>结束当前右键菜单。</summary>
    public static void EndPopup() => NativeGui.EndPopup();

    /// <summary>绘制右键菜单项。</summary>
    public static bool MenuItem(string label, bool enabled = true) => NativeGui.MenuItem(label, enabled);

    /// <summary>写入系统剪贴板。</summary>
    public static void SetClipboardText(string text) => NativeGui.SetClipboardText(text);

    /// <summary>开始禁用控件区域。</summary>
    public static void BeginDisabled(bool disabled = true) => NativeGui.BeginDisabled(disabled);

    /// <summary>结束禁用控件区域。</summary>
    public static void EndDisabled() => NativeGui.EndDisabled();

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
