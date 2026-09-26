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
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, byte, byte, byte*, byte> BeginCollapsibleComponentBlock;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> BeginCombo;
    public delegate* unmanaged[Cdecl]<void> EndCombo;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte> Selectable;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, byte> Checkbox;
    public delegate* unmanaged[Cdecl]<byte*, int, int*, byte> InputInt;
    public delegate* unmanaged[Cdecl]<byte*, int, float*, byte> InputFloat;
    public delegate* unmanaged[Cdecl]<byte*, int, vector3*, byte> InputVector3;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, float, byte, int> InputText;
    public delegate* unmanaged[Cdecl]<void> Separator;
    public delegate* unmanaged[Cdecl]<float, void> SameLine;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte, byte> BeginTable;
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
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte*, int, int> TreeNode;
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
    public delegate* unmanaged[Cdecl]<color*, byte*, int, void> TextColored;
    public delegate* unmanaged[Cdecl]<byte*, int, void> TextWrapped;
    public delegate* unmanaged[Cdecl]<float, void> SetScrollHereY;
    public delegate* unmanaged[Cdecl]<vector2*, void> GetContentRegionAvail;
    public delegate* unmanaged[Cdecl]<vector2*, void> GetCursorScreenPos;
    public delegate* unmanaged[Cdecl]<EditorRectPrimitive*, int, void> DrawRects;
    public delegate* unmanaged[Cdecl]<vector2*, vector2*, vector2*, color*, byte*, int, float, void> DrawTextClipped;
    public delegate* unmanaged[Cdecl]<byte*, int, vector2*, byte> InvisibleButton;
    public delegate* unmanaged[Cdecl]<byte> IsItemHovered;
    public delegate* unmanaged[Cdecl]<byte> IsItemClicked;
    public delegate* unmanaged[Cdecl]<vector2*, void> GetMousePos;
    public delegate* unmanaged[Cdecl]<byte*, int, void> SetTooltip;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, float, byte, int> InputTextMultiline;
    public delegate* unmanaged[Cdecl]<float> GetMouseWheel;
    public delegate* unmanaged[Cdecl]<byte> IsWindowFocused;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte> ToggleButton;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, float, int> RenameInput;
    public delegate* unmanaged[Cdecl]<byte*, int, float, byte> BeginDialog;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, byte*, float, byte, int> AssetRenameTile;
    public delegate* unmanaged[Cdecl]<byte*, int, float*, float, float, float, byte> SliderFloat;
    public delegate* unmanaged[Cdecl]<byte*, int, float> CalcButtonWidth;
    public delegate* unmanaged[Cdecl]<byte*, int, byte, byte> BeginMenu;
    public delegate* unmanaged[Cdecl]<void> EndMenu;
    public delegate* unmanaged[Cdecl]<byte*, int, int*, int, int> BeginList;
    public delegate* unmanaged[Cdecl]<int, byte, byte> ListElement;
    public delegate* unmanaged[Cdecl]<int> EndList;
    public delegate* unmanaged[Cdecl]<byte*, int, void> PushId;
    public delegate* unmanaged[Cdecl]<void> PopId;
}
#pragma warning restore CS0649

//批量矩形绘制单元，字段顺序与原生侧一一对应，全部拍平成 float
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EditorRectPrimitive
{
    public float MinX, MinY, MaxX, MaxY;
    public float R, G, B, A;
    public float Rounding;
}

internal static unsafe class NativeEditorGUI
{
    /// <summary>建立自定义控件身份空间。</summary>
    internal static void PushId(string id)
    {
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) api.PushId(pointer, bytes.Length);
    }

    /// <summary>结束自定义控件身份空间。</summary>
    internal static void PopId() => api.PopId();

    /// <summary>开始带只读数量框的紧凑列表，返回展开标记；始终配对 EndList。</summary>
    internal static int BeginList(string label, ref int count, bool editable, bool mixed, bool canRemove)
    {
        byte[] bytes = Encode(label);
        fixed (byte* text = bytes)
        fixed (int* size = &count) return api.BeginList(text, bytes.Length, size, (editable ? 1 : 0) | (mixed ? 2 : 0) | (canRemove ? 4 : 0));
    }

    /// <summary>绘制列表行的拖动柄与下标，并保持它作为当前拖放目标。</summary>
    internal static bool ListElement(int index, bool selected) => api.ListElement(index, selected ? (byte)1 : (byte)0) != 0;

    /// <summary>结束列表，返回标题栏的增加或删除标记。</summary>
    internal static int EndList() => api.EndList();
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

    //绘制目录节点。返回值：1 展开、2 点击、4 Ctrl、8 双击、16 Alt、32 本次刚切换
    internal static int TreeNode(string label, bool selected, bool leaf = false, bool defaultOpen = false,
        bool forceOpen = false, bool forceCollapse = false, string? icon = null, bool dimmed = false)
    {
        byte[] bytes = Encode(label);
        byte[] iconBytes = Encode(icon);
        fixed (byte* iconPointer = iconBytes)
        fixed (byte* pointer = bytes)
        {
            return api.TreeNode(pointer, bytes.Length, (byte)((selected ? 1 : 0) | (leaf ? 2 : 0)
                | (defaultOpen ? 4 : 0) | (forceOpen ? 8 : 0) | (forceCollapse ? 16 : 0) | (dimmed ? 32 : 0)),
                iconPointer, iconBytes.Length);
        }
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

    //开始固定宽度的模态确认窗；id 里 ### 之前是标题栏文字，之后是稳定 ID
    internal static bool BeginDialog(string id, float width = 0.0f)
    {
        if (!initialized || api.BeginDialog == null) return false;
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) return api.BeginDialog(pointer, bytes.Length, width) != 0;
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

    //开始可折叠组件块，不带激活勾选框
    internal static bool BeginCollapsibleComponentBlock(string? icon, string? title, string? id)
    {
        return BeginCollapsibleComponentBlock(icon, title, id, true, true, false, out bool _);
    }

    //开始带激活勾选框的可折叠组件块；enabled 决定卡片是否压暗，toggled 回传勾选框是否被点
    internal static bool BeginCollapsibleComponentBlock(string? icon,
        string? title,
        string? id,
        bool enabled,
        out bool toggled)
    {
        return BeginCollapsibleComponentBlock(icon, title, id, enabled, true, true, out toggled);
    }

    //开始带激活勾选框、默认折叠的可折叠组件块
    internal static bool BeginCollapsibleComponentBlock(string? icon,
        string? title,
        string? id,
        bool enabled,
        bool defaultOpen,
        out bool toggled)
    {
        return BeginCollapsibleComponentBlock(icon, title, id, enabled, defaultOpen, true, out toggled);
    }

    //上层各入口共用的实现；showToggle 为假时勾选框完全不参与
    private static bool BeginCollapsibleComponentBlock(string? icon,
        string? title,
        string? id,
        bool enabled,
        bool defaultOpen,
        bool showToggle,
        out bool toggled)
    {
        toggled = false;
        if (!initialized || api.BeginCollapsibleComponentBlock == null) return false;

        byte[] iconBytes = Encode(icon);
        byte[] titleBytes = Encode(title);
        byte[] idBytes = Encode(id);
        byte nativeToggled = 0;
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
                enabled ? (byte)1 : (byte)0,
                defaultOpen ? (byte)1 : (byte)0,
                showToggle ? &nativeToggled : null) != 0;
            toggled = nativeToggled != 0;
            return expanded;
        }
    }

    //绘制对象引用框并返回操作：0 无 1 点击 2 清空 3 选择器 4 双击
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

    //绘制资源瓦片：返回 1 选中、2 切换展开、4 鼠标位于展开箭头
    internal static int AssetTile(string? icon, string? label, string? id, float width, bool selected, bool expandable = false, bool expanded = false)
    {
        if (!initialized || api.AssetTile == null) return 0;
        byte[] iconBytes = Encode(icon);
        byte[] labelBytes = Encode(label);
        byte[] idBytes = Encode(id);
        fixed (byte* iconPointer = iconBytes)
        fixed (byte* labelPointer = labelBytes)
        fixed (byte* idPointer = idBytes)
            return api.AssetTile(iconPointer, iconBytes.Length, labelPointer, labelBytes.Length, idPointer, idBytes.Length,
                width, (byte)((selected ? 1 : 0) | (expandable ? 2 : 0) | (expanded ? 4 : 0)));
    }

    //绘制重命名中的资源瓦片：图标照画，名称那一行是输入框。返回 0 继续编辑、1 回车、2 失焦、3 Esc
    internal static int AssetRenameTile(string? icon, string? id, ref string value, ref bool focusRequested, float width, bool selected)
    {
        if (!initialized || api.AssetRenameTile == null) return 0;

        byte[] iconBytes = Encode(icon);
        byte[] idBytes = Encode(id);
        value ??= string.Empty;
        Span<byte> buffer = stackalloc byte[RenameBufferCapacity];
        int maxBytes = buffer.Length - 1;
        Encoder encoder = Encoding.UTF8.GetEncoder();
        encoder.Convert(value.AsSpan(), buffer[..maxBytes], true, out _, out int bytesUsed, out _);
        buffer[bytesUsed] = 0;

        byte focus = focusRequested ? (byte)1 : (byte)0;
        fixed (byte* iconPointer = iconBytes)
        fixed (byte* idPointer = idBytes)
        fixed (byte* bufferPointer = buffer)
        {
            int result = api.AssetRenameTile(iconPointer, iconBytes.Length, idPointer, idBytes.Length,
                bufferPointer, buffer.Length, &focus, width, selected ? (byte)1 : (byte)0);
            focusRequested = focus != 0;
            value = Marshal.PtrToStringUTF8((IntPtr)bufferPointer) ?? string.Empty;
            return result;
        }
    }

    //绘制视图切换按钮
    internal static bool ViewToggleButton(string? id, bool gridMode)
    {
        if (!initialized || api.ViewToggleButton == null) return false;
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) return api.ViewToggleButton(pointer, bytes.Length, gridMode ? (byte)1 : (byte)0) != 0;
    }

    //绘制带颜色文本
    internal static void TextColored(string? text, color value)
    {
        if (!initialized || api.TextColored == null) return;
        byte[] bytes = Encode(text);
        fixed (byte* pointer = bytes) api.TextColored(&value, pointer, bytes.Length);
    }

    //绘制自动换行文本
    internal static void TextWrapped(string? text)
    {
        if (!initialized || api.TextWrapped == null) return;
        byte[] bytes = Encode(text);
        fixed (byte* pointer = bytes) api.TextWrapped(pointer, bytes.Length);
    }

    //把滚动位置移到当前光标处
    internal static void SetScrollHereY(float ratio)
    {
        if (initialized && api.SetScrollHereY != null) api.SetScrollHereY(ratio);
    }

    //读取内容区剩余空间
    internal static vector2 GetContentRegionAvail()
    {
        vector2 size = default;
        if (initialized && api.GetContentRegionAvail != null) api.GetContentRegionAvail(&size);
        return size;
    }

    //读取屏幕坐标下的光标位置
    internal static vector2 GetCursorScreenPos()
    {
        vector2 position = default;
        if (initialized && api.GetCursorScreenPos != null) api.GetCursorScreenPos(&position);
        return position;
    }

    //批量绘制实心矩形
    internal static void DrawRects(EditorRectPrimitive[] rects, int count)
    {
        if (!initialized || api.DrawRects == null || rects.Length == 0 || count <= 0) return;
        fixed (EditorRectPrimitive* pointer = rects) api.DrawRects(pointer, count);
    }

    //在指定位置绘制被裁剪的文本
    internal static void DrawTextClipped(vector2 clipMin, vector2 clipMax, vector2 position, color value, string? text, float fontSize = 0)
    {
        if (!initialized || api.DrawTextClipped == null) return;
        byte[] bytes = Encode(text);
        fixed (byte* pointer = bytes)
            api.DrawTextClipped(&clipMin, &clipMax, &position, &value, pointer, bytes.Length, fontSize);
    }

    //预留一块可交互空白区域
    internal static bool InvisibleButton(string? id, vector2 size)
    {
        if (!initialized || api.InvisibleButton == null) return false;
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) return api.InvisibleButton(pointer, bytes.Length, &size) != 0;
    }

    //判断上一个条目是否悬停
    internal static bool IsItemHovered()
    {
        return initialized && api.IsItemHovered != null && api.IsItemHovered() != 0;
    }

    //判断上一个条目是否被点击
    internal static bool IsItemClicked()
    {
        return initialized && api.IsItemClicked != null && api.IsItemClicked() != 0;
    }

    //读取当前鼠标位置
    internal static vector2 GetMousePos()
    {
        vector2 position = default;
        if (initialized && api.GetMousePos != null) api.GetMousePos(&position);
        return position;
    }

    //显示单行提示
    internal static void SetTooltip(string? text)
    {
        if (!initialized || api.SetTooltip == null) return;
        byte[] bytes = Encode(text);
        fixed (byte* pointer = bytes) api.SetTooltip(pointer, bytes.Length);
    }

    //绘制只读多行文本，可框选复制；缓冲区复用，避免每帧为详情文本分配
    private static byte[] multilineBuffer = new byte[256];

    internal static void InputTextMultiline(string? label, string? text, float height)
    {
        if (!initialized || api.InputTextMultiline == null) return;

        byte[] labelBytes = Encode(label);
        byte[] textBytes = Encode(text);
        int required = textBytes.Length + 1;
        if (multilineBuffer.Length < required) multilineBuffer = new byte[required];

        Array.Copy(textBytes, multilineBuffer, textBytes.Length);
        multilineBuffer[textBytes.Length] = 0;
        if (textBytes.Length + 1 < multilineBuffer.Length) multilineBuffer[textBytes.Length + 1] = 0;

        fixed (byte* labelPointer = labelBytes)
        fixed (byte* bufferPointer = multilineBuffer)
            api.InputTextMultiline(labelPointer, labelBytes.Length, bufferPointer, multilineBuffer.Length, height, 1);
    }

    //行内重命名输入框的缓冲区：够放下一般名称
    private const int RenameBufferCapacity = 256;

    //绘制浮点滑条；width <= 0 时用默认宽度
    internal static bool SliderFloat(string? id, ref float value, float minimum, float maximum, float width = 0.0f)
    {
        if (!initialized || api.SliderFloat == null) return false;
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes)
        fixed (float* valuePointer = &value)
            return api.SliderFloat(pointer, bytes.Length, valuePointer, minimum, maximum, width) != 0;
    }

    //绘制行内重命名输入框；首帧自动聚焦并全选。width <= 0 时占满本行剩余宽度。
    //返回 0 继续编辑、1 回车、2 失焦、3 Esc
    internal static int RenameInput(string? id, ref string value, ref bool focusRequested, float width = 0.0f)
    {
        if (!initialized || api.RenameInput == null) return 0;

        byte[] idBytes = Encode(id);
        value ??= string.Empty;
        Span<byte> buffer = stackalloc byte[RenameBufferCapacity];
        int maxBytes = buffer.Length - 1;
        Encoder encoder = Encoding.UTF8.GetEncoder();
        encoder.Convert(value.AsSpan(), buffer[..maxBytes], true, out _, out int bytesUsed, out _);
        buffer[bytesUsed] = 0;

        byte focus = focusRequested ? (byte)1 : (byte)0;
        fixed (byte* idPointer = idBytes)
        fixed (byte* bufferPointer = buffer)
        {
            int result = api.RenameInput(idPointer, idBytes.Length, bufferPointer, buffer.Length, &focus, width);
            focusRequested = focus != 0;
            value = Marshal.PtrToStringUTF8((IntPtr)bufferPointer) ?? string.Empty;
            return result;
        }
    }

    //读取本帧的鼠标滚轮增量
    internal static float GetMouseWheel()
    {
        return initialized && api.GetMouseWheel != null ? api.GetMouseWheel() : 0.0f;
    }

    //判断当前面板是否拥有焦点
    internal static bool IsWindowFocused()
    {
        return initialized && api.IsWindowFocused != null && api.IsWindowFocused() != 0;
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

    //绘制字符串输入框；width <= 0 时用 ImGui 默认宽度；readOnly 为真时按禁用态压暗且不接受输入
    internal static bool InputText(string? label, ref string value, float width = 0.0f, bool readOnly = false)
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
            int newByteCount = api.InputText(labelPointer, labelBytes.Length, valuePointer, valueBytes.Length, width,
                readOnly ? (byte)1 : (byte)0);
            if (newByteCount < 0) return false;

            newByteCount = Math.Min(newByteCount, maxBytes);
            value = Encoding.UTF8.GetString(valueBytes[..newByteCount]);
            return true;
        }
    }

    //量出按钮将要占用的宽度，供绘制前排版
    internal static float CalcButtonWidth(string? text)
    {
        if (!initialized || api.CalcButtonWidth == null) return 0.0f;
        byte[] bytes = Encode(text);
        fixed (byte* pointer = bytes) return api.CalcButtonWidth(pointer, bytes.Length);
    }

    //绘制分隔线
    internal static void Separator()
    {
        if (initialized && api.Separator != null) api.Separator();
    }

    //切换到同行布局
    internal static void SameLine()
    {
        if (initialized && api.SameLine != null) api.SameLine(-1.0f);
    }

    //切换到同行布局并按偏移定位，用来把控件贴到行的右侧
    internal static void SameLine(float offset)
    {
        if (initialized && api.SameLine != null) api.SameLine(offset);
    }

    //绘制开关按钮：开启时用强调色底
    internal static bool ToggleButton(string? text, bool active)
    {
        if (!initialized || api.ToggleButton == null) return false;
        byte[] bytes = Encode(text);
        fixed (byte* pointer = bytes) return api.ToggleButton(pointer, bytes.Length, active ? (byte)1 : (byte)0) != 0;
    }

    //开始表格
    internal static bool BeginTable(string? id, int columns, bool scroll = true)
    {
        if (!initialized || api.BeginTable == null) return false;
        byte[] bytes = Encode(id);
        fixed (byte* pointer = bytes) return api.BeginTable(pointer, bytes.Length, columns, scroll ? (byte)1 : (byte)0) != 0;
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

    //开始子菜单，返回是否展开；展开时调用方必须配对 EndMenu
    internal static bool BeginMenu(string? label, bool enabled)
    {
        if (!initialized || api.BeginMenu == null) return false;
        byte[] bytes = Encode(label);
        fixed (byte* pointer = bytes) return api.BeginMenu(pointer, bytes.Length, enabled ? (byte)1 : (byte)0) != 0;
    }

    //结束子菜单
    internal static void EndMenu()
    {
        if (initialized && api.EndMenu != null) api.EndMenu();
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
