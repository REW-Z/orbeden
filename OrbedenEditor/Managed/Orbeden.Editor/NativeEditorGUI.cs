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
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, void> BeginComponentBlock;
    public delegate* unmanaged[Cdecl]<void> EndComponentBlock;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, byte, byte*, byte> BeginCollapsibleComponentBlock;
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
    public delegate* unmanaged[Cdecl]<byte*, int, float*, float, byte, byte> BeginChild;
    public delegate* unmanaged[Cdecl]<void> EndChild;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, int> TreeNode;
    public delegate* unmanaged[Cdecl]<void> TreePop;
    public delegate* unmanaged[Cdecl]<byte*, int, void> OpenPopup;
    public delegate* unmanaged[Cdecl]<byte*, int, byte> BeginPopup;
    public delegate* unmanaged[Cdecl]<void> ClosePopup;
    public delegate* unmanaged[Cdecl]<int, byte*, int, void> DragSource;
    public delegate* unmanaged[Cdecl]<int*, byte*, int, int> ReadDrag;
    public delegate* unmanaged[Cdecl]<byte, int, byte> AcceptDrag;
    public delegate* unmanaged[Cdecl]<int> FillRemainingArea;
    public delegate* unmanaged[Cdecl]<int> GetDropPlacement;
    public delegate* unmanaged[Cdecl]<EditorThemeData*, void> SetTheme;
    public delegate* unmanaged[Cdecl]<byte*, int, ulong*, vector2*, byte, byte> BeginPanelContent;
    public delegate* unmanaged[Cdecl]<vector2*, void> EndPanelContent;
    public delegate* unmanaged[Cdecl]<void> DrawSceneView;
    public delegate* unmanaged[Cdecl]<vector3*, byte> ResolveSceneDropPosition;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, int> ReferenceField;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, float, byte, byte> AssetTile;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte> ViewToggleButton;
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

    //从当前条目开始拖动对象或资源
    internal static void DragSource(int kind, string key)
    {
        byte[] bytes = Encode(key);
        fixed (byte* pointer = bytes) api.DragSource(kind, pointer, bytes.Length);
    }

    //读取悬停目标上的共享载荷
    internal static string ReadDrag(out int kind)
    {
        int nativeKind = 0;
        int count = api.ReadDrag(&nativeKind, null, 0);
        byte[] bytes = new byte[count];
        fixed (byte* pointer = bytes) api.ReadDrag(&nativeKind, pointer, count);
        kind = nativeKind;
        return Encoding.UTF8.GetString(bytes);
    }

    //绘制类型匹配预览并接收释放操作
    internal static bool AcceptDrag(bool valid, int placement = 0) => api.AcceptDrag(valid ? (byte)1 : (byte)0, placement) != 0;

    //创建统一面板内容区域
    internal static bool BeginPanelContent(string id, ref ulong host, vector2 scroll, bool restoreScroll)
    {
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes)
        fixed (ulong* hostPointer = &host)
            return api.BeginPanelContent(pointer, bytes.Length, hostPointer, &scroll, restoreScroll ? (byte)1 : (byte)0) != 0;
    }

    //结束面板内容并读取滚动位置
    internal static vector2 EndPanelContent()
    {
        vector2 scroll = default;
        api.EndPanelContent(&scroll);
        return scroll;
    }

    //绘制 Scene 面板的原生场景视口
    internal static void DrawSceneView()
    {
        if (initialized && api.DrawSceneView != null) api.DrawSceneView();
    }

    //解析场景视口当前鼠标位置的投放点
    internal static bool ResolveSceneDropPosition(out vector3 position)
    {
        position = default;
        if (!initialized || api.ResolveSceneDropPosition == null) return false;
        vector3 resolved = default;
        if (api.ResolveSceneDropPosition(&resolved) == 0) return false;
        position = resolved;
        return true;
    }

    //提交共享主题参数
    internal static void SetTheme(EditorThemeData value)
    {
        if (initialized && api.SetTheme != null) api.SetTheme(&value);
    }

    //绘制空白投放区域并读取点击状态
    internal static int FillRemainingArea() => api.FillRemainingArea();

    //获取节点前后或子级投放位置
    internal static int GetDropPlacement() => api.GetDropPlacement();

    //开始可调整宽度的独立滚动区域
    internal static bool BeginChild(string id, ref float width, float height = 0, bool resizable = false)
    {
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes)
        fixed (float* size = &width)
            return api.BeginChild(pointer, bytes.Length, size, height, resizable ? (byte)1 : (byte)0) != 0;
    }

    //结束滚动区域
    internal static void EndChild() => api.EndChild();

    //绘制目录节点并返回展开与点击状态
    internal static int TreeNode(string label, bool selected, bool leaf = false, bool defaultOpen = false)
    {
        byte[] bytes = Encode(label);
        fixed (byte* pointer = bytes) return api.TreeNode(pointer, bytes.Length, (byte)((selected ? 1 : 0) | (leaf ? 2 : 0) | (defaultOpen ? 4 : 0)));
    }

    //结束目录节点
    internal static void TreePop() => api.TreePop();

    //打开确认弹窗
    internal static void OpenPopup(string id)
    {
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) api.OpenPopup(pointer, bytes.Length);
    }

    //开始模态弹窗
    internal static bool BeginPopup(string id)
    {
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) return api.BeginPopup(pointer, bytes.Length) != 0;
    }

    //关闭当前弹窗
    internal static void ClosePopup() => api.ClosePopup();

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
    internal static void BeginComponentBlock(string? icon, string? title)
    {
        if (!initialized || api.BeginComponentBlock == null) return;
        byte[] iconBytes = Encode(icon);
        byte[] titleBytes = Encode(title);
        fixed (byte* iconPointer = iconBytes)
        fixed (byte* titlePointer = titleBytes)
            api.BeginComponentBlock(iconPointer, iconBytes.Length, titlePointer, titleBytes.Length);
    }

    //结束组件块
    internal static void EndComponentBlock()
    {
        if (initialized && api.EndComponentBlock != null) api.EndComponentBlock();
    }

    //开始可折叠组件块
    internal static bool BeginCollapsibleComponentBlock(string? icon,
        string? title,
        string? id,
        bool removable,
        out bool removeRequested)
    {
        removeRequested = false;
        if (!initialized || api.BeginCollapsibleComponentBlock == null) return false;

        byte[] iconBytes = Encode(icon);
        byte[] titleBytes = Encode(title);
        byte[] idBytes = Encode(id);
        byte nativeRemoveRequested = 0;
        fixed (byte* iconPointer = iconBytes)
        fixed (byte* titlePointer = titleBytes)
        fixed (byte* idPointer = idBytes)
        {
            bool expanded = api.BeginCollapsibleComponentBlock(
                iconPointer,
                iconBytes.Length,
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

    //绘制对象引用框并返回操作：0 无 1 点击 2 清空 3 选择器
    internal static int ReferenceField(string? icon, string? text, string? id)
    {
        if (!initialized || api.ReferenceField == null) return 0;
        byte[] iconBytes = Encode(icon);
        byte[] textBytes = Encode(text);
        byte[] idBytes = Encode(id);
        fixed (byte* iconPointer = iconBytes)
        fixed (byte* textPointer = textBytes)
        fixed (byte* idPointer = idBytes)
            return api.ReferenceField(iconPointer, iconBytes.Length, textPointer, textBytes.Length, idPointer, idBytes.Length);
    }

    //绘制资源瓦片
    internal static bool AssetTile(string? icon, string? label, string? id, float width, bool selected)
    {
        if (!initialized || api.AssetTile == null) return false;
        byte[] iconBytes = Encode(icon);
        byte[] labelBytes = Encode(label);
        byte[] idBytes = Encode(id);
        fixed (byte* iconPointer = iconBytes)
        fixed (byte* labelPointer = labelBytes)
        fixed (byte* idPointer = idBytes)
            return api.AssetTile(iconPointer, iconBytes.Length, labelPointer, labelBytes.Length, idPointer, idBytes.Length,
                width, selected ? (byte)1 : (byte)0) != 0;
    }

    //绘制视图切换按钮
    internal static bool ViewToggleButton(string? id, bool gridMode)
    {
        if (!initialized || api.ViewToggleButton == null) return false;
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) return api.ViewToggleButton(pointer, bytes.Length, gridMode ? (byte)1 : (byte)0) != 0;
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
