using System;
using System.Runtime.InteropServices;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorGuiNativeApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, void> Label;
    public delegate* unmanaged[Cdecl]<byte*, int, byte> Button;
    public delegate* unmanaged[Cdecl]<byte*, int, void> BeginComponentBlock;
    public delegate* unmanaged[Cdecl]<void> EndComponentBlock;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte, byte*, byte> BeginCollapsibleComponentBlock;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> BeginCombo;
    public delegate* unmanaged[Cdecl]<void> EndCombo;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte> Selectable;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, byte> Checkbox;
    public delegate* unmanaged[Cdecl]<byte*, int, int*, byte> InputInt;
    public delegate* unmanaged[Cdecl]<byte*, int, float*, byte> InputFloat;
    public delegate* unmanaged[Cdecl]<byte*, int, vector3*, byte> InputVector3;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> InputText;
    public delegate* unmanaged[Cdecl]<void> Separator;
    public delegate* unmanaged[Cdecl]<void> SameLine;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte> BeginTable;
    public delegate* unmanaged[Cdecl]<void> EndTable;
    public delegate* unmanaged[Cdecl]<byte*, int, float, byte, void> TableSetupColumn;
    public delegate* unmanaged[Cdecl]<void> TableHeadersRow;
    public delegate* unmanaged[Cdecl]<void> TableNextRow;
    public delegate* unmanaged[Cdecl]<int, void> TableSetColumnIndex;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte, byte> TableSelectable;
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

internal static unsafe class NativeEditorGUI
{
    private static EditorGuiNativeApi api;
    private static bool initialized;

    //保存 EditorGUI 函数表
    internal static void Initialize(EditorGuiNativeApi value)
    {
        api = value;
        initialized = api.Label != null;
    }

    //编码 UTF-8 文本
    private static byte[] Encode(string? text)
    {
        return Encoding.UTF8.GetBytes(text ?? string.Empty);
    }

    //绘制文本标签
    internal static void Label(string? text)
    {
        if (!initialized || api.Label == null) return;
        byte[] bytes = Encode(text);
        fixed (byte* pointer = bytes) api.Label(pointer, bytes.Length);
    }

    //绘制按钮
    internal static bool Button(string? text)
    {
        if (!initialized || api.Button == null) return false;
        byte[] bytes = Encode(text);
        fixed (byte* pointer = bytes) return api.Button(pointer, bytes.Length) != 0;
    }

    //开始组件块
    internal static void BeginComponentBlock(string? title)
    {
        if (!initialized || api.BeginComponentBlock == null) return;
        byte[] bytes = Encode(title);
        fixed (byte* pointer = bytes) api.BeginComponentBlock(pointer, bytes.Length);
    }

    //结束组件块
    internal static void EndComponentBlock()
    {
        if (initialized && api.EndComponentBlock != null) api.EndComponentBlock();
    }

    //开始可折叠组件块
    internal static bool BeginCollapsibleComponentBlock(string? title,
        string? id,
        bool removable,
        out bool removeRequested)
    {
        removeRequested = false;
        if (!initialized || api.BeginCollapsibleComponentBlock == null) return false;

        byte[] titleBytes = Encode(title);
        byte[] idBytes = Encode(id);
        byte nativeRemoveRequested = 0;
        fixed (byte* titlePointer = titleBytes)
        fixed (byte* idPointer = idBytes)
        {
            bool expanded = api.BeginCollapsibleComponentBlock(
                titlePointer,
                titleBytes.Length,
                idPointer,
                idBytes.Length,
                removable ? (byte)1 : (byte)0,
                &nativeRemoveRequested) != 0;
            removeRequested = nativeRemoveRequested != 0;
            return expanded;
        }
    }

    //开始下拉选择框
    internal static bool BeginCombo(string? label, string? preview)
    {
        if (!initialized || api.BeginCombo == null) return false;
        byte[] labelBytes = Encode(label);
        byte[] previewBytes = Encode(preview);
        fixed (byte* labelPointer = labelBytes)
        fixed (byte* previewPointer = previewBytes)
        {
            return api.BeginCombo(labelPointer, labelBytes.Length, previewPointer, previewBytes.Length) != 0;
        }
    }

    //结束下拉选择框
    internal static void EndCombo()
    {
        if (initialized && api.EndCombo != null) api.EndCombo();
    }

    //绘制选择项
    internal static bool Selectable(string? label, bool selected)
    {
        if (!initialized || api.Selectable == null) return false;
        byte[] bytes = Encode(label);
        fixed (byte* pointer = bytes) return api.Selectable(pointer, bytes.Length, selected ? (byte)1 : (byte)0) != 0;
    }

    //绘制布尔输入框
    internal static bool Checkbox(string? label, ref bool value)
    {
        if (!initialized || api.Checkbox == null) return false;
        byte[] bytes = Encode(label);
        byte nativeValue = value ? (byte)1 : (byte)0;
        fixed (byte* pointer = bytes)
        {
            bool changed = api.Checkbox(pointer, bytes.Length, &nativeValue) != 0;
            value = nativeValue != 0;
            return changed;
        }
    }

    //绘制整数输入框
    internal static bool InputInt(string? label, ref int value)
    {
        if (!initialized || api.InputInt == null) return false;
        byte[] bytes = Encode(label);
        fixed (byte* pointer = bytes)
        fixed (int* valuePointer = &value)
        {
            return api.InputInt(pointer, bytes.Length, valuePointer) != 0;
        }
    }

    //绘制浮点输入框
    internal static bool InputFloat(string? label, ref float value)
    {
        if (!initialized || api.InputFloat == null) return false;
        byte[] bytes = Encode(label);
        fixed (byte* pointer = bytes)
        fixed (float* valuePointer = &value)
        {
            return api.InputFloat(pointer, bytes.Length, valuePointer) != 0;
        }
    }

    //绘制三维向量输入框
    internal static bool InputVector3(string? label, ref vector3 value)
    {
        if (!initialized || api.InputVector3 == null) return false;
        byte[] bytes = Encode(label);
        fixed (byte* pointer = bytes)
        fixed (vector3* valuePointer = &value)
        {
            return api.InputVector3(pointer, bytes.Length, valuePointer) != 0;
        }
    }

    //绘制字符串输入框
    internal static bool InputText(string? label, ref string value)
    {
        if (!initialized || api.InputText == null) return false;

        byte[] labelBytes = Encode(label);
        value ??= string.Empty;
        Span<byte> valueBytes = stackalloc byte[256];
        int maxBytes = valueBytes.Length - 1;
        Encoder encoder = Encoding.UTF8.GetEncoder();
        encoder.Convert(value.AsSpan(), valueBytes[..maxBytes], true, out _, out int bytesUsed, out _);
        valueBytes[bytesUsed] = 0;

        fixed (byte* labelPointer = labelBytes)
        fixed (byte* valuePointer = valueBytes)
        {
            int newByteCount = api.InputText(labelPointer, labelBytes.Length, valuePointer, valueBytes.Length);
            if (newByteCount < 0) return false;

            newByteCount = Math.Min(newByteCount, maxBytes);
            value = Encoding.UTF8.GetString(valueBytes[..newByteCount]);
            return true;
        }
    }

    //绘制分隔线
    internal static void Separator()
    {
        if (initialized && api.Separator != null) api.Separator();
    }

    //切换到同行布局
    internal static void SameLine()
    {
        if (initialized && api.SameLine != null) api.SameLine();
    }

    //开始表格
    internal static bool BeginTable(string? id, int columns)
    {
        if (!initialized || api.BeginTable == null) return false;
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) return api.BeginTable(pointer, bytes.Length, columns) != 0;
    }

    //结束表格
    internal static void EndTable()
    {
        if (initialized && api.EndTable != null) api.EndTable();
    }

    //配置表格列
    internal static void TableSetupColumn(string? label, float width, bool fixedWidth)
    {
        if (!initialized || api.TableSetupColumn == null) return;
        byte[] bytes = Encode(label);
        fixed (byte* pointer = bytes)
        {
            api.TableSetupColumn(pointer, bytes.Length, width, fixedWidth ? (byte)1 : (byte)0);
        }
    }

    //绘制表头
    internal static void TableHeadersRow()
    {
        if (initialized && api.TableHeadersRow != null) api.TableHeadersRow();
    }

    //前进到下一表格行
    internal static void TableNextRow()
    {
        if (initialized && api.TableNextRow != null) api.TableNextRow();
    }

    //切换当前表格列
    internal static void TableSetColumnIndex(int column)
    {
        if (initialized && api.TableSetColumnIndex != null) api.TableSetColumnIndex(column);
    }

    //绘制表格选择项
    internal static bool TableSelectable(string? label, bool selected, bool spanAllColumns)
    {
        if (!initialized || api.TableSelectable == null) return false;
        byte[] bytes = Encode(label);
        fixed (byte* pointer = bytes)
        {
            return api.TableSelectable(pointer,
                bytes.Length,
                selected ? (byte)1 : (byte)0,
                spanAllColumns ? (byte)1 : (byte)0) != 0;
        }
    }

    //判断控件双击
    internal static bool IsItemDoubleClicked()
    {
        return initialized && api.IsItemDoubleClicked != null && api.IsItemDoubleClicked() != 0;
    }

    //开始控件右键菜单
    internal static bool BeginPopupContextItem(string? id)
    {
        if (!initialized || api.BeginPopupContextItem == null) return false;
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) return api.BeginPopupContextItem(pointer, bytes.Length) != 0;
    }

    //开始窗口右键菜单
    internal static bool BeginPopupContextWindow(string? id)
    {
        if (!initialized || api.BeginPopupContextWindow == null) return false;
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) return api.BeginPopupContextWindow(pointer, bytes.Length) != 0;
    }

    //结束右键菜单
    internal static void EndPopup()
    {
        if (initialized && api.EndPopup != null) api.EndPopup();
    }

    //绘制菜单项
    internal static bool MenuItem(string? label, bool enabled)
    {
        if (!initialized || api.MenuItem == null) return false;
        byte[] bytes = Encode(label);
        fixed (byte* pointer = bytes) return api.MenuItem(pointer, bytes.Length, enabled ? (byte)1 : (byte)0) != 0;
    }

    //写入剪贴板文本
    internal static void SetClipboardText(string? text)
    {
        if (!initialized || api.SetClipboardText == null) return;
        byte[] bytes = Encode(text);
        fixed (byte* pointer = bytes) api.SetClipboardText(pointer, bytes.Length);
    }

    //开始禁用控件区域
    internal static void BeginDisabled(bool disabled)
    {
        if (initialized && api.BeginDisabled != null) api.BeginDisabled(disabled ? (byte)1 : (byte)0);
    }

    //结束禁用控件区域
    internal static void EndDisabled()
    {
        if (initialized && api.EndDisabled != null) api.EndDisabled();
    }
}
