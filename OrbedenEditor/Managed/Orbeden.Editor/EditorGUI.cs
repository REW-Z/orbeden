using System;
using System.Collections.Generic;
using Orbeden;

namespace OrbedenEditor;

/// <summary>Editor Immediate GUI API。</summary>
public static class EditorGUI
{
    private static IObjectFieldAssetProvider? objectFieldAssetProvider;

    /// <summary>设置资源字段数据源。</summary>
    internal static void SetObjectFieldAssetProvider(IObjectFieldAssetProvider? provider)
    {
        objectFieldAssetProvider = provider;
        EditorObjectField.Clear();
    }

    /// <summary>绘制文本标签。</summary>
    public static void Label(string text) => NativeEditorGUI.Label(text);

    /// <summary>绘制 Scene 面板的原生场景视口。</summary>
    public static void DrawSceneView() => NativeEditorGUI.DrawSceneView();

    /// <summary>解析场景视口当前鼠标位置的投放点。</summary>
    internal static bool ResolveSceneDropPosition(out vector3 position) => NativeEditorGUI.ResolveSceneDropPosition(out position);

    /// <summary>绘制按钮。</summary>
    public static bool Button(string text) => NativeEditorGUI.Button(text);

    /// <summary>开始组件块，未指定图标时使用通用图标。</summary>
    public static void BeginComponentBlock(string title) => NativeEditorGUI.BeginComponentBlock("Other", title);

    /// <summary>开始带图标的组件块。</summary>
    public static void BeginComponentBlock(string title, string icon) => NativeEditorGUI.BeginComponentBlock(icon, title);

    /// <summary>结束组件块。</summary>
    public static void EndComponentBlock() => NativeEditorGUI.EndComponentBlock();

    /// <summary>开始可折叠组件块，未指定图标时使用通用图标。</summary>
    public static bool BeginCollapsibleComponentBlock(string title, string id)
    {
        return NativeEditorGUI.BeginCollapsibleComponentBlock("Other", title, id);
    }

    /// <summary>开始带图标与显示名称的可折叠组件块。</summary>
    public static bool BeginCollapsibleComponentBlock(string title, string icon, string id)
    {
        return NativeEditorGUI.BeginCollapsibleComponentBlock(icon, title, id);
    }

    /// <summary>开始带激活勾选框的可折叠组件块；未激活时整张卡片压暗。</summary>
    public static bool BeginCollapsibleComponentBlock(string title,
        string icon,
        string id,
        bool enabled,
        out bool toggled)
    {
        return NativeEditorGUI.BeginCollapsibleComponentBlock(icon, title, id, enabled, out toggled);
    }

    /// <summary>开始带激活勾选框的可折叠组件块，并可指定默认是展开还是折叠（没有记住状态时生效）。</summary>
    public static bool BeginCollapsibleComponentBlock(string title,
        string icon,
        string id,
        bool enabled,
        bool defaultOpen,
        out bool toggled)
    {
        return NativeEditorGUI.BeginCollapsibleComponentBlock(icon, title, id, enabled, defaultOpen, out toggled);
    }

    /// <summary>开始下拉选择框。</summary>
    public static bool BeginCombo(string label, string preview) => NativeEditorGUI.BeginCombo(label, preview);

    /// <summary>结束下拉选择框。</summary>
    public static void EndCombo() => NativeEditorGUI.EndCombo();

    /// <summary>绘制选择项。</summary>
    public static bool Selectable(string label, bool selected = false) => NativeEditorGUI.Selectable(label, selected);

    /// <summary>绘制资源瓦片，整块作为一个条目。</summary>
    public static bool AssetTile(string icon, string label, string id, float width, bool selected = false)
        => (NativeEditorGUI.AssetTile(icon, label, id, width, selected) & 1) != 0;

    /// <summary>绘制重命名中的资源瓦片：图标照画，名称那一行是输入框。返回 0 继续编辑、1 回车、2 失焦、3 Esc。</summary>
    public static int AssetRenameTile(string icon, string id, ref string value, ref bool focusRequested, float width, bool selected)
        => NativeEditorGUI.AssetRenameTile(icon, id, ref value, ref focusRequested, width, selected);

    /// <summary>绘制视图切换按钮，按钮上是当前模式的图标。</summary>
    public static bool ViewToggleButton(string id, bool gridMode) => NativeEditorGUI.ViewToggleButton(id, gridMode);

    /// <summary>绘制强类型资源字段。</summary>
    public static bool ObjectField<T>(string label, ref T? value) where T : Orbeden.Object
    {
        Orbeden.Object? objectValue = value;
        bool changed = ObjectField(label, typeof(T), ref objectValue);
        if (changed) value = objectValue as T;
        return changed;
    }

    /// <summary>加载资源字段对象。</summary>
    public static Orbeden.Object? LoadObjectFieldAsset(Type objectType, string resourceKey)
    {
        if (string.IsNullOrEmpty(resourceKey)) return null;
        return objectFieldAssetProvider?.Load(objectType, resourceKey);
    }

    /// <summary>绘制运行时类型资源字段。</summary>
    public static bool ObjectField(string label, Type objectType, ref Orbeden.Object? value)
    {
        string resourceKey = value?.ResourceKey ?? string.Empty;
        return ObjectField(label, objectType, ref value, ref resourceKey);
    }

    /// <summary>绘制保留资源 Key 的资源字段。</summary>
    public static bool ObjectField(string label,
        Type objectType,
        ref Orbeden.Object? value,
        ref string resourceKey)
    {
        if (!typeof(Orbeden.Object).IsAssignableFrom(objectType)) return false;
        int id = value?.InstanceId ?? 0;
        if (!EditorObjectField.Draw(label, objectType.FullName ?? objectType.Name, ref resourceKey, ref id)) return false;
        value = id == 0 ? null : NativeBindingRuntime.Wrap<Orbeden.Object>(id);
        return true;
    }

    //读取资源选择器候选项
    internal static IReadOnlyList<ObjectFieldOption> GetObjectFieldAssets(Type type)
        => objectFieldAssetProvider?.GetAssets(type) ?? [];

    /// <summary>绘制分隔线。</summary>
    public static void Separator() => NativeEditorGUI.Separator();

    /// <summary>切换到同行布局。</summary>
    public static void SameLine() => NativeEditorGUI.SameLine();

    /// <summary>切换到同行布局并按偏移定位。</summary>
    public static void SameLine(float offset) => NativeEditorGUI.SameLine(offset);

    /// <summary>绘制开关按钮；开启时用强调色底。</summary>
    public static bool ToggleButton(string text, bool active) => NativeEditorGUI.ToggleButton(text, active);

    /// <summary>绘制行内重命名输入框；width 为 0 时占满本行剩余宽度。返回 0 继续编辑、1 回车、2 失焦、3 Esc。</summary>
    public static int RenameInput(string id, ref string value, ref bool focusRequested, float width = 0.0f)
        => NativeEditorGUI.RenameInput(id, ref value, ref focusRequested, width);

    /// <summary>开始表格。</summary>
    public static bool BeginTable(string id, int columns) => NativeEditorGUI.BeginTable(id, columns);

    /// <summary>结束表格。</summary>
    public static void EndTable() => NativeEditorGUI.EndTable();

    /// <summary>配置表格列。</summary>
    public static void TableSetupColumn(string label, float width = 0.0f, bool fixedWidth = false)
        => NativeEditorGUI.TableSetupColumn(label, width, fixedWidth);

    /// <summary>绘制表头。</summary>
    public static void TableHeadersRow() => NativeEditorGUI.TableHeadersRow();

    /// <summary>前进到下一表格行。</summary>
    public static void TableNextRow() => NativeEditorGUI.TableNextRow();

    /// <summary>切换当前表格列。</summary>
    public static void TableSetColumnIndex(int column) => NativeEditorGUI.TableSetColumnIndex(column);

    /// <summary>绘制表格选择项。</summary>
    public static bool TableSelectable(string label, bool selected = false, bool spanAllColumns = true)
        => NativeEditorGUI.TableSelectable(label, selected, spanAllColumns);

    /// <summary>判断控件是否被双击。</summary>
    public static bool IsItemDoubleClicked() => NativeEditorGUI.IsItemDoubleClicked();

    /// <summary>开始控件右键菜单。</summary>
    public static bool BeginPopupContextItem(string id) => NativeEditorGUI.BeginPopupContextItem(id);

    /// <summary>开始窗口右键菜单。</summary>
    public static bool BeginPopupContextWindow(string id) => NativeEditorGUI.BeginPopupContextWindow(id);

    /// <summary>结束右键菜单。</summary>
    public static void EndPopup() => NativeEditorGUI.EndPopup();

    /// <summary>绘制菜单项。</summary>
    public static bool MenuItem(string label, bool enabled = true) => NativeEditorGUI.MenuItem(label, enabled);

    /// <summary>开始子菜单，返回是否展开；展开时必须配对调用 <see cref="EndMenu"/>。</summary>
    public static bool BeginMenu(string label, bool enabled = true) => NativeEditorGUI.BeginMenu(label, enabled);

    /// <summary>结束子菜单。</summary>
    public static void EndMenu() => NativeEditorGUI.EndMenu();

    /// <summary>写入剪贴板文本。</summary>
    public static void SetClipboardText(string text) => NativeEditorGUI.SetClipboardText(text);

    /// <summary>开始禁用控件区域。</summary>
    public static void BeginDisabled(bool disabled = true) => NativeEditorGUI.BeginDisabled(disabled);

    /// <summary>结束禁用控件区域。</summary>
    public static void EndDisabled() => NativeEditorGUI.EndDisabled();

    /// <summary>绘制布尔输入框。</summary>
    public static bool Checkbox(string label, ref bool value) => NativeEditorGUI.Checkbox(label, ref value);

    /// <summary>绘制整数输入框。</summary>
    public static bool InputInt(string label, ref int value) => NativeEditorGUI.InputInt(label, ref value);

    /// <summary>绘制浮点滑条。</summary>
    public static bool SliderFloat(string id, ref float value, float minimum, float maximum, float width = 0.0f)
        => NativeEditorGUI.SliderFloat(id, ref value, minimum, maximum, width);

    /// <summary>绘制浮点输入框。</summary>
    public static bool InputFloat(string label, ref float value) => NativeEditorGUI.InputFloat(label, ref value);

    /// <summary>绘制三维向量输入框。</summary>
    public static bool InputVector3(string label, ref vector3 value) => NativeEditorGUI.InputVector3(label, ref value);

    /// <summary>绘制字符串输入框；width 为 0 时用 ImGui 默认宽度；readOnly 为真时按禁用态压暗且不接受输入。</summary>
    public static bool InputText(string label, ref string value, float width = 0.0f, bool readOnly = false)
        => NativeEditorGUI.InputText(label, ref value, width, readOnly);

    /// <summary>量出按钮将要占用的宽度，供绘制前把一行控件排到指定位置。</summary>
    public static float CalcButtonWidth(string text) => NativeEditorGUI.CalcButtonWidth(text);

    /// <summary>绘制带颜色文本。</summary>
    public static void TextColored(string text, color value) => NativeEditorGUI.TextColored(text, value);

    /// <summary>绘制自动换行文本。</summary>
    public static void TextWrapped(string text) => NativeEditorGUI.TextWrapped(text);

    /// <summary>把滚动位置移到当前光标处。</summary>
    public static void SetScrollHereY(float ratio) => NativeEditorGUI.SetScrollHereY(ratio);

    /// <summary>显示单行提示。</summary>
    public static void SetTooltip(string text) => NativeEditorGUI.SetTooltip(text);
}

public readonly struct ObjectFieldOption
{
    public string ResourceKey { get; }
    public string DisplayName { get; }

    /// <summary>创建资源字段选项。</summary>
    public ObjectFieldOption(string resourceKey, string displayName)
    {
        ResourceKey = resourceKey;
        DisplayName = displayName;
    }
}

public interface IObjectFieldAssetProvider
{
    /// <summary>读取指定类型的资源字段选项。</summary>
    IReadOnlyList<ObjectFieldOption> GetAssets(Type objectType);

    /// <summary>加载资源字段对象。</summary>
    Orbeden.Object? Load(Type objectType, string resourceKey);
}
