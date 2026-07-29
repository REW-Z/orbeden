using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct RuntimeGuiApi
{
    //所有字符串参数均为 UTF-8 字节，长度不包含结尾零字符。
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

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct RuntimeGuiExtensionApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte, byte*, byte> BeginCollapsibleComponentBlock;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> BeginCombo;
    public delegate* unmanaged[Cdecl]<void> EndCombo;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte> Selectable;
}

[StructLayout(LayoutKind.Sequential)]
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

internal static unsafe class NativeGui
{
    private static RuntimeGuiApi api;
    private static RuntimeGuiExtensionApi extensionApi;
    private static RuntimeGuiAdvancedApi advancedApi;
    private static bool initialized;

    //保存 C++ 传入的 Runtime GUI 函数表
    internal static void Initialize(RuntimeGuiApi value, RuntimeGuiExtensionApi extensionValue, RuntimeGuiAdvancedApi advancedValue)
    {
        api = value;
        extensionApi = extensionValue;
        advancedApi = advancedValue;
        initialized = api.Label != null;
    }

    //绘制文本标签
    internal static void Label(string? text)
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
    internal static bool Button(string? text)
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
    internal static bool BeginPanel(string? title)
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
    internal static void EndPanel()
    {
        if (!initialized || api.EndPanel == null) return;
        api.EndPanel();
    }

    //开始绘制一个组件块
    internal static void BeginComponentBlock(string? title)
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
    internal static void EndComponentBlock()
    {
        if (!initialized || api.EndComponentBlock == null) return;
        api.EndComponentBlock();
    }

    //开始绘制可折叠、可选移除的组件块
    internal static bool BeginCollapsibleComponentBlock(string? title, string? id, bool removable, out bool removeRequested)
    {
        removeRequested = false;
        if (!initialized) return false;
        if (extensionApi.BeginCollapsibleComponentBlock == null)
        {
            BeginComponentBlock(title);
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
    internal static bool BeginCombo(string? label, string? preview)
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
    internal static void EndCombo()
    {
        if (!initialized || extensionApi.EndCombo == null) return;
        extensionApi.EndCombo();
    }

    //绘制下拉选择项
    internal static bool Selectable(string? label, bool selected)
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
    internal static void Separator()
    {
        if (initialized && advancedApi.Separator != null) advancedApi.Separator();
    }

    //让下一个控件与前一个控件同行。
    internal static void SameLine()
    {
        if (initialized && advancedApi.SameLine != null) advancedApi.SameLine();
    }

    //开始一个表格。
    internal static bool BeginTable(string? id, int columns)
    {
        if (!initialized || advancedApi.BeginTable == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(id ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return advancedApi.BeginTable(pointer, bytes.Length, columns) != 0;
        }
    }

    //结束当前表格。
    internal static void EndTable()
    {
        if (initialized && advancedApi.EndTable != null) advancedApi.EndTable();
    }

    //配置一个表格列。
    internal static void TableSetupColumn(string? label, float width, bool fixedWidth)
    {
        if (!initialized || advancedApi.TableSetupColumn == null) return;

        byte[] bytes = Encoding.UTF8.GetBytes(label ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            advancedApi.TableSetupColumn(pointer, bytes.Length, width, fixedWidth ? (byte)1 : (byte)0);
        }
    }

    //绘制表头。
    internal static void TableHeadersRow()
    {
        if (initialized && advancedApi.TableHeadersRow != null) advancedApi.TableHeadersRow();
    }

    //前进到下一表格行。
    internal static void TableNextRow()
    {
        if (initialized && advancedApi.TableNextRow != null) advancedApi.TableNextRow();
    }

    //切换当前表格列。
    internal static void TableSetColumnIndex(int column)
    {
        if (initialized && advancedApi.TableSetColumnIndex != null) advancedApi.TableSetColumnIndex(column);
    }

    //绘制支持表格跨列的选择项。
    internal static bool TableSelectable(string? label, bool selected, bool spanAllColumns)
    {
        if (!initialized || advancedApi.Selectable == null) return Selectable(label, selected);

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
    internal static bool IsItemDoubleClicked()
    {
        return initialized && advancedApi.IsItemDoubleClicked != null && advancedApi.IsItemDoubleClicked() != 0;
    }

    //开始刚绘制控件的右键菜单。
    internal static bool BeginPopupContextItem(string? id)
    {
        if (!initialized || advancedApi.BeginPopupContextItem == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(id ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return advancedApi.BeginPopupContextItem(pointer, bytes.Length) != 0;
        }
    }

    //开始当前窗口空白区域的右键菜单。
    internal static bool BeginPopupContextWindow(string? id)
    {
        if (!initialized || advancedApi.BeginPopupContextWindow == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(id ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return advancedApi.BeginPopupContextWindow(pointer, bytes.Length) != 0;
        }
    }

    //结束当前右键菜单。
    internal static void EndPopup()
    {
        if (initialized && advancedApi.EndPopup != null) advancedApi.EndPopup();
    }

    //绘制右键菜单项。
    internal static bool MenuItem(string? label, bool enabled)
    {
        if (!initialized || advancedApi.MenuItem == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(label ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return advancedApi.MenuItem(pointer, bytes.Length, enabled ? (byte)1 : (byte)0) != 0;
        }
    }

    //写入系统剪贴板。
    internal static void SetClipboardText(string? text)
    {
        if (!initialized || advancedApi.SetClipboardText == null) return;

        byte[] bytes = Encoding.UTF8.GetBytes(text ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            advancedApi.SetClipboardText(pointer, bytes.Length);
        }
    }

    //开始禁用控件区域。
    internal static void BeginDisabled(bool disabled)
    {
        if (initialized && advancedApi.BeginDisabled != null) advancedApi.BeginDisabled(disabled ? (byte)1 : (byte)0);
    }

    //结束禁用控件区域。
    internal static void EndDisabled()
    {
        if (initialized && advancedApi.EndDisabled != null) advancedApi.EndDisabled();
    }

    //绘制布尔输入框
    internal static bool Checkbox(string? label, ref bool value)
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
    internal static bool InputInt(string? label, ref int value)
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
    internal static bool InputFloat(string? label, ref float value)
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
    internal static bool InputVector3(string? label, ref vector3 value)
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
    internal static bool InputText(string? label, ref string value)
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
