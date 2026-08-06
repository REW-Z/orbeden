using System;
using System.Collections.Generic;
using Orbeden;

namespace OrbedenEditor;

/// <summary>Editor Immediate GUI API。</summary>
public static class EditorGUI
{
    private static readonly Dictionary<string, string> ObjectFieldSearches = [];
    private static IObjectFieldAssetProvider? objectFieldAssetProvider;

    /// <summary>设置资源字段数据源。</summary>
    internal static void SetObjectFieldAssetProvider(IObjectFieldAssetProvider? provider)
    {
        objectFieldAssetProvider = provider;
        ObjectFieldSearches.Clear();
    }

    /// <summary>绘制文本标签。</summary>
    public static void Label(string text) => NativeEditorGUI.Label(text);

    /// <summary>绘制按钮。</summary>
    public static bool Button(string text) => NativeEditorGUI.Button(text);

    /// <summary>开始组件块。</summary>
    public static void BeginComponentBlock(string title) => NativeEditorGUI.BeginComponentBlock(title);

    /// <summary>结束组件块。</summary>
    public static void EndComponentBlock() => NativeEditorGUI.EndComponentBlock();

    /// <summary>开始可折叠组件块。</summary>
    public static bool BeginCollapsibleComponentBlock(string title,
        string id,
        bool removable,
        out bool removeRequested)
    {
        return NativeEditorGUI.BeginCollapsibleComponentBlock(title, id, removable, out removeRequested);
    }

    /// <summary>开始下拉选择框。</summary>
    public static bool BeginCombo(string label, string preview) => NativeEditorGUI.BeginCombo(label, preview);

    /// <summary>结束下拉选择框。</summary>
    public static void EndCombo() => NativeEditorGUI.EndCombo();

    /// <summary>绘制选择项。</summary>
    public static bool Selectable(string label, bool selected = false) => NativeEditorGUI.Selectable(label, selected);

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
        if (objectType == null || !typeof(Orbeden.Object).IsAssignableFrom(objectType)) return false;

        IReadOnlyList<ObjectFieldOption> options = objectFieldAssetProvider?.GetAssets(objectType) ?? [];
        string currentKey = resourceKey ?? string.Empty;
        string preview = string.IsNullOrEmpty(currentKey)
            ? $"None ({objectType.Name})"
            : GetObjectFieldDisplayName(currentKey, options);
        if (!BeginCombo(label, preview)) return false;

        bool changed = false;
        try
        {
            string searchId = $"Search##object_field_{label}";
            string search = ObjectFieldSearches.TryGetValue(label, out string? savedSearch)
                ? savedSearch
                : string.Empty;
            if (InputText(searchId, ref search)) ObjectFieldSearches[label] = search;

            if (Selectable($"None ({objectType.Name})##object_field_none_{label}", string.IsNullOrEmpty(currentKey)))
            {
                value = null;
                resourceKey = string.Empty;
                changed = !string.IsNullOrEmpty(currentKey);
            }

            foreach (ObjectFieldOption option in options)
            {
                if (!MatchesObjectFieldSearch(option, search)) continue;

                bool selected = string.Equals(option.ResourceKey, currentKey, StringComparison.Ordinal);
                if (!Selectable($"{option.DisplayName}##object_field_{label}_{option.ResourceKey}", selected)) continue;

                Orbeden.Object? loaded = objectFieldAssetProvider?.Load(objectType, option.ResourceKey);
                if (loaded == null || !objectType.IsInstanceOfType(loaded)) continue;

                value = loaded;
                resourceKey = option.ResourceKey;
                changed = !selected || currentKey.Length == 0;
            }
        }
        finally
        {
            EndCombo();
        }

        return changed;
    }

    //读取资源字段显示名
    private static string GetObjectFieldDisplayName(string resourceKey, IReadOnlyList<ObjectFieldOption> options)
    {
        foreach (ObjectFieldOption option in options)
        {
            if (string.Equals(option.ResourceKey, resourceKey, StringComparison.Ordinal)) return option.DisplayName;
        }
        return $"Missing: {resourceKey}";
    }

    //匹配资源字段搜索文本
    private static bool MatchesObjectFieldSearch(ObjectFieldOption option, string search)
    {
        if (string.IsNullOrWhiteSpace(search)) return true;
        return option.DisplayName.Contains(search.Trim(), StringComparison.OrdinalIgnoreCase)
            || option.ResourceKey.Contains(search.Trim(), StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>绘制分隔线。</summary>
    public static void Separator() => NativeEditorGUI.Separator();

    /// <summary>切换到同行布局。</summary>
    public static void SameLine() => NativeEditorGUI.SameLine();

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

    /// <summary>绘制浮点输入框。</summary>
    public static bool InputFloat(string label, ref float value) => NativeEditorGUI.InputFloat(label, ref value);

    /// <summary>绘制三维向量输入框。</summary>
    public static bool InputVector3(string label, ref vector3 value) => NativeEditorGUI.InputVector3(label, ref value);

    /// <summary>绘制字符串输入框。</summary>
    public static bool InputText(string label, ref string value) => NativeEditorGUI.InputText(label, ref value);
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
