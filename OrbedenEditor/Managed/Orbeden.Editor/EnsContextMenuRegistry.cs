using Orbeden;
using System.Reflection;

namespace OrbedenEditor;

/// <summary>EnsViewPanel 右键菜单收到的节点上下文。</summary>
public readonly struct EnsContext
{
    /// <summary>节点身份，在面板空白处为无效值。</summary>
    public EnsId Id { get; }

    /// <summary>节点稳定 Key，在面板空白处为空串。</summary>
    public string ResourceKey { get; }

    /// <summary>节点名称，在面板空白处为空串。</summary>
    public string Name { get; }

    /// <summary>是否指向一个真实节点。</summary>
    public bool IsValid { get; }

    /// <summary>创建一个 Ens 右键上下文。</summary>
    public EnsContext(EnsId id, string resourceKey, string name, bool isValid)
    {
        Id = id;
        ResourceKey = resourceKey;
        Name = name;
        IsValid = isValid;
    }
}

/// <summary>允许 Editor 扩展向 EnsViewPanel 追加右键菜单项。</summary>
public static class EnsContextMenuRegistry
{
    private sealed record Item(string Label, Func<EnsContext, bool>? Enabled, Action<EnsContext> Execute, Assembly? Source);
    private static readonly List<Item> Items = [];

    /// <summary>注册一个 EnsViewPanel 右键菜单项。</summary>
    public static void Register(string label, Action<EnsContext> execute, Func<EnsContext, bool>? enabled = null)
    {
        if (string.IsNullOrWhiteSpace(label)) throw new ArgumentException("Menu label is empty.", nameof(label));
        ArgumentNullException.ThrowIfNull(execute);
        //记下来源程序集：卸载时按程序集整批摘掉，避免重复菜单与强引用泄漏。
        Items.Add(new Item(label, enabled, execute, execute.Method.DeclaringType?.Assembly));
    }

    /// <summary>注销一个菜单项；重复注销无副作用。</summary>
    public static void Unregister(string label)
    {
        for (int index = Items.Count - 1; index >= 0; --index)
        {
            if (string.Equals(Items[index].Label, label, StringComparison.Ordinal)) Items.RemoveAt(index);
        }
    }

    /// <summary>注销某个程序集登记的全部菜单项；程序集卸载时调用。</summary>
    public static void UnregisterAssembly(Assembly assembly)
    {
        if (assembly == null) return;
        for (int index = Items.Count - 1; index >= 0; --index)
        {
            if (ReferenceEquals(Items[index].Source, assembly)) Items.RemoveAt(index);
        }
    }

    /// <summary>清空全部菜单项。</summary>
    public static void Clear() => Items.Clear();

    /// <summary>当前登记的菜单项数量。</summary>
    public static int Count => Items.Count;

    //绘制所有扩展菜单项。
    internal static void Draw(EnsContext context, Action<string> report)
    {
        if (Items.Count == 0) return;
        EditorGUI.Separator();
        foreach (Item item in Items)
        {
            bool enabled = item.Enabled?.Invoke(context) ?? true;
            if (!EditorGUI.MenuItem(item.Label, enabled)) continue;
            try
            {
                item.Execute(context);
            }
            catch (Exception ex)
            {
                report($"{item.Label} failed: {ex.Message}");
            }
        }
    }
}
