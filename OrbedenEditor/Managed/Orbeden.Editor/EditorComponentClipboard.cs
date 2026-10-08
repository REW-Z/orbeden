using Orbeden;

namespace OrbedenEditor;

/// <summary>一次组件复制的内容：类型身份加一份字段值快照。</summary>
internal sealed record ComponentClipboardEntry(
    string TypeName,
    bool IsManaged,
    bool CopiedAsNew,
    IReadOnlyList<(string Name, InteropValueKind Kind, InteropValue Value)> Values);

/// <summary>
/// 组件复制粘贴的进程内剪贴板。只活在本次编辑器会话内，不经过系统剪贴板：
/// 粘贴是按类型新建组件再回填字段值，而不是回放组件快照，所以不需要可序列化的载体。
/// </summary>
internal static class EditorComponentClipboard
{
    internal static ComponentClipboardEntry? Entry { get; private set; }

    /// <summary>剪贴板里是否有可粘贴的字段值。</summary>
    internal static bool HasValues => Entry is { Values.Count: > 0 };

    /// <summary>剪贴板里的内容能否作为新组件粘贴。</summary>
    internal static bool CanPasteAsNew => Entry is { CopiedAsNew: true, TypeName.Length: > 0 };

    /// <summary>剪贴板里的内容是否与给定组件同类型，同类型才允许粘贴字段值。</summary>
    internal static bool Matches(NativeComponentInfo component)
    {
        return Entry != null
            && Entry.IsManaged == component.IsManaged
            && string.Equals(Entry.TypeName, component.TypeName, StringComparison.Ordinal);
    }

    /// <summary>按组件当前快照记录字段值；asNew 为真表示这份内容可以整组件粘贴。</summary>
    internal static void Capture(NativeComponentInfo component, bool asNew)
    {
        NativeComponentPropertyTarget target = new(component, static () => { });
        target.Refresh();
        List<(string, InteropValueKind, InteropValue)> values = [];
        foreach (PropertyDescriptor descriptor in target.Properties)
        {
            if (target.TryGet(descriptor.Name, out InteropValue value) != InteropStatus.Ok) continue;
            values.Add((descriptor.Name, value.Kind, value));
        }
        Entry = new ComponentClipboardEntry(component.TypeName, component.IsManaged, asNew, values);
    }

    internal static void Clear() => Entry = null;
}
