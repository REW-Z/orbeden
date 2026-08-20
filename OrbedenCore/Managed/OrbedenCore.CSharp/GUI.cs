using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

/// <summary>运行时 Immediate GUI API。</summary>
public static partial class GUI
{
    /// <summary>绘制文本标签。</summary>
    public static void Label(string text)
    {
        NativeLabel(text);
    }

    /// <summary>绘制按钮并返回本帧是否点击。</summary>
    public static bool Button(string text)
    {
        return NativeButton(text);
    }

    /// <summary>开始一个浮动面板，调用后必须匹配 EndPanel。</summary>
    public static bool BeginPanel(string title)
    {
        return NativeBeginPanel(title);
    }

    /// <summary>结束当前浮动面板。</summary>
    public static void EndPanel()
    {
        NativeEndPanel();
    }

    /// <summary>开始绘制一个组件块。</summary>
    public static void BeginComponentBlock(string title)
    {
        NativeBeginComponentBlock(title);
    }

    /// <summary>结束当前组件块。</summary>
    public static void EndComponentBlock()
    {
        NativeEndComponentBlock();
    }

    /// <summary>开始一个可折叠、可选移除的组件块。</summary>
    public static bool BeginCollapsibleComponentBlock(string title, string id, bool removable, out bool removeRequested)
    {
        return NativeBeginCollapsibleComponentBlock(title, id, removable, out removeRequested);
    }

    /// <summary>开始一个下拉选择框，返回本帧是否展开。</summary>
    public static bool BeginCombo(string label, string preview)
    {
        return NativeBeginCombo(label, preview);
    }

    /// <summary>结束当前下拉选择框。</summary>
    public static void EndCombo()
    {
        NativeEndCombo();
    }

    /// <summary>绘制下拉选择项并返回本帧是否点击。</summary>
    public static bool Selectable(string label, bool selected = false)
    {
        return NativeSelectable(label, selected);
    }

    /// <summary>绘制分隔线。</summary>
    public static void Separator() => NativeSeparator();

    /// <summary>让下一个控件与前一个控件同行。</summary>
    public static void SameLine() => NativeSameLine();

    /// <summary>开始一个可滚动表格。</summary>
    public static bool BeginTable(string id, int columns) => NativeBeginTable(id, columns);

    /// <summary>结束当前表格。</summary>
    public static void EndTable() => NativeEndTable();

    /// <summary>配置一个表格列。</summary>
    public static void TableSetupColumn(string label, float width = 0.0f, bool fixedWidth = false)
        => NativeTableSetupColumn(label, width, fixedWidth);

    /// <summary>绘制当前表格表头。</summary>
    public static void TableHeadersRow() => NativeTableHeadersRow();

    /// <summary>前进到下一表格行。</summary>
    public static void TableNextRow() => NativeTableNextRow();

    /// <summary>切换当前表格列。</summary>
    public static void TableSetColumnIndex(int column) => NativeTableSetColumnIndex(column);

    /// <summary>绘制支持跨表格列的选择项。</summary>
    public static bool TableSelectable(string label, bool selected = false, bool spanAllColumns = true)
        => NativeTableSelectable(label, selected, spanAllColumns);

    /// <summary>判断刚绘制的控件是否被左键双击。</summary>
    public static bool IsItemDoubleClicked() => NativeIsItemDoubleClicked();

    /// <summary>开始刚绘制控件的右键菜单。</summary>
    public static bool BeginPopupContextItem(string id) => NativeBeginPopupContextItem(id);

    /// <summary>开始当前窗口空白区域的右键菜单。</summary>
    public static bool BeginPopupContextWindow(string id) => NativeBeginPopupContextWindow(id);

    /// <summary>结束当前右键菜单。</summary>
    public static void EndPopup() => NativeEndPopup();

    /// <summary>绘制右键菜单项。</summary>
    public static bool MenuItem(string label, bool enabled = true) => NativeMenuItem(label, enabled);

    /// <summary>写入系统剪贴板。</summary>
    public static void SetClipboardText(string text) => NativeSetClipboardText(text);

    /// <summary>开始禁用控件区域。</summary>
    public static void BeginDisabled(bool disabled = true) => NativeBeginDisabled(disabled);

    /// <summary>结束禁用控件区域。</summary>
    public static void EndDisabled() => NativeEndDisabled();

    /// <summary>绘制布尔输入框。</summary>
    public static bool Checkbox(string label, ref bool value)
    {
        return NativeCheckbox(label, ref value);
    }

    /// <summary>绘制整数输入框。</summary>
    public static bool InputInt(string label, ref int value)
    {
        return NativeInputInt(label, ref value);
    }

    /// <summary>绘制浮点输入框。</summary>
    public static bool InputFloat(string label, ref float value)
    {
        return NativeInputFloat(label, ref value);
    }

    /// <summary>绘制三维向量输入框。</summary>
    public static bool InputVector3(string label, ref vector3 value)
    {
        return NativeInputVector3(label, ref value);
    }

    /// <summary>绘制字符串输入框。</summary>
    public static bool InputText(string label, ref string value)
    {
        return NativeInputText(label, ref value);
    }
}

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct RuntimeGuiApi
{
    //定义运行时 GUI 函数表
    public delegate* unmanaged[Cdecl]<byte*, int, void> Label;
    public delegate* unmanaged[Cdecl]<byte*, int, byte> Button;
    public delegate* unmanaged[Cdecl]<byte*, int, byte> BeginPanel;
    public delegate* unmanaged[Cdecl]<void> EndPanel;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, byte> Checkbox;
    public delegate* unmanaged[Cdecl]<byte*, int, int*, byte> InputInt;
    public delegate* unmanaged[Cdecl]<byte*, int, float*, byte> InputFloat;
    public delegate* unmanaged[Cdecl]<byte*, int, vector3*, byte> InputVector3;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> InputText;
    public delegate* unmanaged[Cdecl]<byte*, int, void> BeginComponentBlock;
    public delegate* unmanaged[Cdecl]<void> EndComponentBlock;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct RuntimeGuiExtensionApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte, byte*, byte> BeginCollapsibleComponentBlock;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> BeginCombo;
    public delegate* unmanaged[Cdecl]<void> EndCombo;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte> Selectable;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct RuntimeGuiAdvancedApi
{
    public delegate* unmanaged[Cdecl]<void> Separator;
    public delegate* unmanaged[Cdecl]<void> SameLine;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte> BeginTable;
    public delegate* unmanaged[Cdecl]<void> EndTable;
    public delegate* unmanaged[Cdecl]<byte*, int, float, byte, void> TableSetupColumn;
    public delegate* unmanaged[Cdecl]<void> TableHeadersRow;
    public delegate* unmanaged[Cdecl]<void> TableNextRow;
    public delegate* unmanaged[Cdecl]<int, void> TableSetColumnIndex;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte, byte> Selectable;
    public delegate* unmanaged[Cdecl]<byte> IsItemDoubleClicked;
    public delegate* unmanaged[Cdecl]<byte*, int, byte> BeginPopupContextItem;
    public delegate* unmanaged[Cdecl]<byte*, int, byte> BeginPopupContextWindow;
    public delegate* unmanaged[Cdecl]<void> EndPopup;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte> MenuItem;
    public delegate* unmanaged[Cdecl]<byte*, int, void> SetClipboardText;
    public delegate* unmanaged[Cdecl]<byte, void> BeginDisabled;
    public delegate* unmanaged[Cdecl]<void> EndDisabled;
}
#pragma warning restore CS0649

public static unsafe partial class GUI
{
private static RuntimeGuiApi api;
    private static RuntimeGuiExtensionApi extensionApi;
    private static RuntimeGuiAdvancedApi advancedApi;
    private static bool initialized;

    //保存 C++ 传入的 Runtime GUI 函数表
    internal static void InitializeNativeApi(RuntimeGuiApi value, RuntimeGuiExtensionApi extensionValue, RuntimeGuiAdvancedApi advancedValue)
    {
        api = value;
        extensionApi = extensionValue;
        advancedApi = advancedValue;
        initialized = api.Label != null;
    }

    //绘制文本标签
    internal static void NativeLabel(string? text)
    {
        if (!initialized || api.Label == null) return;

        string value = text ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            api.Label(pointer, byteCount);
        }
    }

    //绘制按钮并返回是否点击
    internal static bool NativeButton(string? text)
    {
        if (!initialized || api.Button == null) return false;

        string value = text ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            return api.Button(pointer, byteCount) != 0;
        }
    }

    //开始一个浮动面板
    internal static bool NativeBeginPanel(string? title)
    {
        if (!initialized || api.BeginPanel == null) return false;

        string value = title ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            return api.BeginPanel(pointer, byteCount) != 0;
        }
    }

    //结束一个浮动面板
    internal static void NativeEndPanel()
    {
        if (!initialized || api.EndPanel == null) return;
        api.EndPanel();
    }

    //开始绘制一个组件块
    internal static void NativeBeginComponentBlock(string? title)
    {
        if (!initialized || api.BeginComponentBlock == null) return;

        string value = title ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            api.BeginComponentBlock(pointer, byteCount);
        }
    }

    //结束当前组件块
    internal static void NativeEndComponentBlock()
    {
        if (!initialized || api.EndComponentBlock == null) return;
        api.EndComponentBlock();
    }

    //开始绘制可折叠、可选移除的组件块
    internal static bool NativeBeginCollapsibleComponentBlock(string? title, string? id, bool removable, out bool removeRequested)
    {
        removeRequested = false;
        if (!initialized) return false;
        if (extensionApi.BeginCollapsibleComponentBlock == null)
        {
            NativeBeginComponentBlock(title);
            return true;
        }

        string titleText = title ?? string.Empty;
        int titleByteCount = Encoding.UTF8.GetByteCount(titleText);
        Span<byte> titleBytes = titleByteCount <= 1024 ? stackalloc byte[Math.Max(titleByteCount, 1)] : new byte[titleByteCount];
        Encoding.UTF8.GetBytes(titleText.AsSpan(), titleBytes);

        string idText = id ?? string.Empty;
        int idByteCount = Encoding.UTF8.GetByteCount(idText);
        Span<byte> idBytes = idByteCount <= 1024 ? stackalloc byte[Math.Max(idByteCount, 1)] : new byte[idByteCount];
        Encoding.UTF8.GetBytes(idText.AsSpan(), idBytes);

        byte nativeRemoveRequested = 0;
        fixed (byte* titlePointer = titleBytes)
        fixed (byte* idPointer = idBytes)
        {
            bool expanded = extensionApi.BeginCollapsibleComponentBlock(titlePointer,
                titleByteCount,
                idPointer,
                idByteCount,
                removable ? (byte)1 : (byte)0,
                &nativeRemoveRequested) != 0;
            removeRequested = nativeRemoveRequested != 0;
            return expanded;
        }
    }

    //开始绘制下拉选择框
    internal static bool NativeBeginCombo(string? label, string? preview)
    {
        if (!initialized || extensionApi.BeginCombo == null) return false;

        string labelText = label ?? string.Empty;
        int labelByteCount = Encoding.UTF8.GetByteCount(labelText);
        Span<byte> labelBytes = labelByteCount <= 1024 ? stackalloc byte[Math.Max(labelByteCount, 1)] : new byte[labelByteCount];
        Encoding.UTF8.GetBytes(labelText.AsSpan(), labelBytes);

        string previewText = preview ?? string.Empty;
        int previewByteCount = Encoding.UTF8.GetByteCount(previewText);
        Span<byte> previewBytes = previewByteCount <= 1024 ? stackalloc byte[Math.Max(previewByteCount, 1)] : new byte[previewByteCount];
        Encoding.UTF8.GetBytes(previewText.AsSpan(), previewBytes);

        fixed (byte* labelPointer = labelBytes)
        fixed (byte* previewPointer = previewBytes)
        {
            return extensionApi.BeginCombo(labelPointer, labelByteCount, previewPointer, previewByteCount) != 0;
        }
    }

    //结束当前下拉选择框
    internal static void NativeEndCombo()
    {
        if (!initialized || extensionApi.EndCombo == null) return;
        extensionApi.EndCombo();
    }

    //绘制下拉选择项
    internal static bool NativeSelectable(string? label, bool selected)
    {
        if (!initialized || extensionApi.Selectable == null) return false;

        string value = label ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            return extensionApi.Selectable(pointer, byteCount, selected ? (byte)1 : (byte)0) != 0;
        }
    }

    //绘制分隔线。
    internal static void NativeSeparator()
    {
        if (initialized && advancedApi.Separator != null) advancedApi.Separator();
    }

    //让下一个控件与前一个控件同行。
    internal static void NativeSameLine()
    {
        if (initialized && advancedApi.SameLine != null) advancedApi.SameLine();
    }

    //开始一个表格。
    internal static bool NativeBeginTable(string? id, int columns)
    {
        if (!initialized || advancedApi.BeginTable == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(id ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return advancedApi.BeginTable(pointer, bytes.Length, columns) != 0;
        }
    }

    //结束当前表格。
    internal static void NativeEndTable()
    {
        if (initialized && advancedApi.EndTable != null) advancedApi.EndTable();
    }

    //配置一个表格列。
    internal static void NativeTableSetupColumn(string? label, float width, bool fixedWidth)
    {
        if (!initialized || advancedApi.TableSetupColumn == null) return;

        byte[] bytes = Encoding.UTF8.GetBytes(label ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            advancedApi.TableSetupColumn(pointer, bytes.Length, width, fixedWidth ? (byte)1 : (byte)0);
        }
    }

    //绘制表头。
    internal static void NativeTableHeadersRow()
    {
        if (initialized && advancedApi.TableHeadersRow != null) advancedApi.TableHeadersRow();
    }

    //前进到下一表格行。
    internal static void NativeTableNextRow()
    {
        if (initialized && advancedApi.TableNextRow != null) advancedApi.TableNextRow();
    }

    //切换当前表格列。
    internal static void NativeTableSetColumnIndex(int column)
    {
        if (initialized && advancedApi.TableSetColumnIndex != null) advancedApi.TableSetColumnIndex(column);
    }

    //绘制支持表格跨列的选择项。
    internal static bool NativeTableSelectable(string? label, bool selected, bool spanAllColumns)
    {
        if (!initialized || advancedApi.Selectable == null) return NativeSelectable(label, selected);

        byte[] bytes = Encoding.UTF8.GetBytes(label ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return advancedApi.Selectable(pointer,
                bytes.Length,
                selected ? (byte)1 : (byte)0,
                spanAllColumns ? (byte)1 : (byte)0) != 0;
        }
    }

    //判断刚绘制的控件是否被双击。
    internal static bool NativeIsItemDoubleClicked()
    {
        return initialized && advancedApi.IsItemDoubleClicked != null && advancedApi.IsItemDoubleClicked() != 0;
    }

    //开始刚绘制控件的右键菜单。
    internal static bool NativeBeginPopupContextItem(string? id)
    {
        if (!initialized || advancedApi.BeginPopupContextItem == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(id ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return advancedApi.BeginPopupContextItem(pointer, bytes.Length) != 0;
        }
    }

    //开始当前窗口空白区域的右键菜单。
    internal static bool NativeBeginPopupContextWindow(string? id)
    {
        if (!initialized || advancedApi.BeginPopupContextWindow == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(id ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return advancedApi.BeginPopupContextWindow(pointer, bytes.Length) != 0;
        }
    }

    //结束当前右键菜单。
    internal static void NativeEndPopup()
    {
        if (initialized && advancedApi.EndPopup != null) advancedApi.EndPopup();
    }

    //绘制右键菜单项。
    internal static bool NativeMenuItem(string? label, bool enabled)
    {
        if (!initialized || advancedApi.MenuItem == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(label ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return advancedApi.MenuItem(pointer, bytes.Length, enabled ? (byte)1 : (byte)0) != 0;
        }
    }

    //写入系统剪贴板。
    internal static void NativeSetClipboardText(string? text)
    {
        if (!initialized || advancedApi.SetClipboardText == null) return;

        byte[] bytes = Encoding.UTF8.GetBytes(text ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            advancedApi.SetClipboardText(pointer, bytes.Length);
        }
    }

    //开始禁用控件区域。
    internal static void NativeBeginDisabled(bool disabled)
    {
        if (initialized && advancedApi.BeginDisabled != null) advancedApi.BeginDisabled(disabled ? (byte)1 : (byte)0);
    }

    //结束禁用控件区域。
    internal static void NativeEndDisabled()
    {
        if (initialized && advancedApi.EndDisabled != null) advancedApi.EndDisabled();
    }

    //绘制布尔输入框
    internal static bool NativeCheckbox(string? label, ref bool value)
    {
        if (!initialized || api.Checkbox == null) return false;

        string text = label ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(text);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(text.AsSpan(), bytes);

        byte nativeValue = value ? (byte)1 : (byte)0;
        fixed (byte* pointer = bytes)
        {
            bool changed = api.Checkbox(pointer, byteCount, &nativeValue) != 0;
            value = nativeValue != 0;
            return changed;
        }
    }

    //绘制整数输入框
    internal static bool NativeInputInt(string? label, ref int value)
    {
        if (!initialized || api.InputInt == null) return false;

        string text = label ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(text);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(text.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            fixed (int* valuePointer = &value)
            {
                return api.InputInt(pointer, byteCount, valuePointer) != 0;
            }
        }
    }

    //绘制浮点输入框
    internal static bool NativeInputFloat(string? label, ref float value)
    {
        if (!initialized || api.InputFloat == null) return false;

        string text = label ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(text);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(text.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            fixed (float* valuePointer = &value)
            {
                return api.InputFloat(pointer, byteCount, valuePointer) != 0;
            }
        }
    }

    //绘制三维向量输入框
    internal static bool NativeInputVector3(string? label, ref vector3 value)
    {
        if (!initialized || api.InputVector3 == null) return false;

        string text = label ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(text);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(text.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            fixed (vector3* valuePointer = &value)
            {
                return api.InputVector3(pointer, byteCount, valuePointer) != 0;
            }
        }
    }

    //绘制字符串输入框
    internal static bool NativeInputText(string? label, ref string value)
    {
        if (!initialized || api.InputText == null) return false;

        string text = label ?? string.Empty;
        int labelByteCount = Encoding.UTF8.GetByteCount(text);
        Span<byte> labelBytes = labelByteCount <= 1024 ? stackalloc byte[Math.Max(labelByteCount, 1)] : new byte[labelByteCount];
        Encoding.UTF8.GetBytes(text.AsSpan(), labelBytes);

        value ??= string.Empty;
        Span<byte> valueBytes = stackalloc byte[256];
        int maxValueBytes = valueBytes.Length - 1;
        int valueCharCount = value.Length;
        if (Encoding.UTF8.GetByteCount(value) > maxValueBytes)
        {
            int low = 0;
            int high = value.Length;
            while (low < high)
            {
                int candidate = low + (high - low + 1) / 2;
                if (Encoding.UTF8.GetByteCount(value.AsSpan(0, candidate)) <= maxValueBytes) low = candidate;
                else high = candidate - 1;
            }

            valueCharCount = low;
            if (valueCharCount > 0
                && valueCharCount < value.Length
                && char.IsHighSurrogate(value[valueCharCount - 1])
                && char.IsLowSurrogate(value[valueCharCount]))
            {
                valueCharCount--;
            }
        }

        int valueByteCount = Encoding.UTF8.GetBytes(value.AsSpan(0, valueCharCount), valueBytes);
        valueBytes[valueByteCount] = 0;

        fixed (byte* labelPointer = labelBytes)
        fixed (byte* valuePointer = valueBytes)
        {
            int newByteCount = api.InputText(labelPointer, labelByteCount, valuePointer, valueBytes.Length);
            if (newByteCount < 0) return false;

            newByteCount = Math.Min(newByteCount, valueBytes.Length - 1);
            string newValue = Encoding.UTF8.GetString(valueBytes[..newByteCount]);
            bool changed = newValue != value;
            value = newValue;
            return changed;
        }
    }
}
