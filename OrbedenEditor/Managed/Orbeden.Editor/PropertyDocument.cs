using Orbeden;

namespace OrbedenEditor;

internal readonly record struct PropertyDescriptor(string Name, InteropValueKind Kind);

internal interface IPropertyTarget
{
    string Identity { get; }
    IReadOnlyList<PropertyDescriptor> Properties { get; }
    InteropStatus TryGet(string name, out InteropValue value);
    InteropStatus Validate(string name, InteropValue value);
    InteropStatus Set(string name, InteropValue value);
    void MarkDirty();
}

/// <summary>一次或多次编辑器修改组成的可撤销事务。</summary>
internal sealed class EditorChange
{
    internal string Label = string.Empty;
    internal string MergeKey = string.Empty;
    internal Action Undo = null!;
    internal Action Redo = null!;
    internal DateTime TimestampUtc;
}

/// <summary>Editor 全局属性与组件事务历史，最多保存 256 条。</summary>
public static class EditorPropertyHistory
{
    private const int Capacity = 256;
    private static readonly List<EditorChange> undo = [];
    private static readonly List<EditorChange> redo = [];

    public static bool CanUndo => undo.Count != 0;
    public static bool CanRedo => redo.Count != 0;

    internal static void Push(EditorChange change, bool merge = false)
    {
        DateTime now = DateTime.UtcNow;
        bool canMerge = merge
            && undo.Count != 0
            && !string.IsNullOrEmpty(change.MergeKey)
            && undo[^1].MergeKey == change.MergeKey
            && now - undo[^1].TimestampUtc <= TimeSpan.FromMilliseconds(750);
        change.TimestampUtc = now;
        if (canMerge)
        {
            EditorChange old = undo[^1];
            undo[^1] = new EditorChange { Label = change.Label, MergeKey = change.MergeKey,
                Undo = old.Undo, Redo = change.Redo, TimestampUtc = now };
        }
        else
        {
            undo.Add(change);
            if (undo.Count > Capacity) undo.RemoveAt(0);
        }
        redo.Clear();
    }

    public static bool Undo()
    {
        if (undo.Count == 0) return false;
        EditorChange change = undo[^1];
        undo.RemoveAt(undo.Count - 1);
        change.Undo();
        redo.Add(change);
        EditorApplication.RequestRepaint();
        return true;
    }

    public static bool Redo()
    {
        if (redo.Count == 0) return false;
        EditorChange change = redo[^1];
        redo.RemoveAt(redo.Count - 1);
        change.Redo();
        undo.Add(change);
        EditorApplication.RequestRepaint();
        return true;
    }

    public static void Clear()
    {
        undo.Clear();
        redo.Clear();
    }

    internal static void PushAction(string label, Action undoAction, Action redoAction, string mergeKey = "", bool merge = false)
    {
        Push(new EditorChange { Label = label, MergeKey = mergeKey,
            Undo = undoAction, Redo = redoAction, TimestampUtc = DateTime.UtcNow }, merge);
    }
}

/// <summary>PropertyDocument 中一个可暂存、多目标混合的顶层属性。</summary>
public sealed class PropertyValue
{
    private readonly PropertyDocument document;
    private InteropValue value;

    public string Name { get; }
    public InteropValueKind Kind { get; }
    public bool HasMultipleDifferentValues { get; internal set; }
    public InteropValue Value => value;
    internal bool Modified { get; private set; }

    internal PropertyValue(PropertyDocument owner, string name, InteropValueKind kind)
    {
        document = owner;
        Name = name;
        Kind = kind;
    }

    public void SetValue(InteropValue newValue)
    {
        if (newValue.Kind != Kind) throw new ArgumentException($"Property '{Name}' expects {Kind}, got {newValue.Kind}.", nameof(newValue));
        value = newValue;
        HasMultipleDifferentValues = false;
        Modified = true;
        document.MarkModified();
    }

    internal void SetSnapshot(InteropValue newValue, bool mixed)
    {
        value = newValue;
        HasMultipleDifferentValues = mixed;
        Modified = false;
    }

    internal void ClearModified()
    {
        Modified = false;
    }
}

/// <summary>统一编辑 C++ 活组件、C# sidecar 和 PIE 托管组件的事务文档。</summary>
public sealed class PropertyDocument
{
    private readonly IReadOnlyList<IPropertyTarget> targets;
    private readonly List<PropertyValue> properties = [];
    private bool modified;

    internal PropertyDocument(IReadOnlyList<IPropertyTarget> propertyTargets)
    {
        targets = propertyTargets;
    }

    public IReadOnlyList<PropertyValue> Properties => properties;
    public bool HasPendingChanges => modified;

    public void Update()
    {
        properties.Clear();
        modified = false;
        if (targets.Count == 0) return;

        Dictionary<string, InteropValueKind> common = new(StringComparer.Ordinal);
        foreach (PropertyDescriptor descriptor in targets[0].Properties) common[descriptor.Name] = descriptor.Kind;
        for (int index = 1; index < targets.Count; ++index)
        {
            Dictionary<string, InteropValueKind> current = new(StringComparer.Ordinal);
            foreach (PropertyDescriptor descriptor in targets[index].Properties)
            {
                current[descriptor.Name] = descriptor.Kind;
            }
            foreach (string name in common.Keys.ToArray())
            {
                if (!current.TryGetValue(name, out InteropValueKind kind) || kind != common[name]) common.Remove(name);
            }
        }

        foreach ((string name, InteropValueKind kind) in common.OrderBy(pair => pair.Key, StringComparer.Ordinal))
        {
            InteropValue first = default;
            bool hasFirst = false;
            bool mixed = false;
            bool readable = true;
            foreach (IPropertyTarget target in targets)
            {
                if (target.TryGet(name, out InteropValue current) != InteropStatus.Ok)
                {
                    readable = false;
                    break;
                }
                if (!hasFirst)
                {
                    first = current;
                    hasFirst = true;
                }
                else if (!first.Equals(current)) mixed = true;
            }
            if (!readable || !hasFirst) continue;

            PropertyValue property = new(this, name, kind);
            property.SetSnapshot(first, mixed);
            properties.Add(property);
        }
    }

    public PropertyValue? FindProperty(string name)
    {
        return properties.FirstOrDefault(property => string.Equals(property.Name, name, StringComparison.Ordinal));
    }

    public bool ApplyChanges(string undoLabel)
    {
        List<PropertyValue> changes = properties.Where(property => property.Modified).ToList();
        if (changes.Count == 0) return true;

        List<(IPropertyTarget Target, string Name, InteropValue OldValue, InteropValue NewValue)> writes = [];
        foreach (PropertyValue property in changes)
        {
            foreach (IPropertyTarget target in targets)
            {
                if (target.Validate(property.Name, property.Value) != InteropStatus.Ok) return false;
                if (target.TryGet(property.Name, out InteropValue oldValue) != InteropStatus.Ok) return false;
                writes.Add((target, property.Name, oldValue, property.Value));
            }
        }

        int applied = 0;
        for (; applied < writes.Count; ++applied)
        {
            var write = writes[applied];
            if (write.Target.Set(write.Name, write.NewValue) == InteropStatus.Ok) continue;
            for (int rollback = applied - 1; rollback >= 0; --rollback)
            {
                var old = writes[rollback];
                old.Target.Set(old.Name, old.OldValue);
            }
            return false;
        }

        foreach (IPropertyTarget target in targets.Distinct()) target.MarkDirty();
        string mergeKey = string.Join('|', writes.Select(write => $"{write.Target.Identity}:{write.Name}"));
        EditorPropertyHistory.PushAction(
            undoLabel,
            () => ApplyHistory(writes, useNewValue: false),
            () => ApplyHistory(writes, useNewValue: true),
            mergeKey,
            merge: true);

        foreach (PropertyValue property in changes) property.ClearModified();
        modified = false;
        Update();
        return true;
    }

    internal void MarkModified()
    {
        modified = true;
    }

    private static void ApplyHistory(IReadOnlyList<(IPropertyTarget Target, string Name, InteropValue OldValue, InteropValue NewValue)> writes, bool useNewValue)
    {
        foreach (var write in writes)
        {
            write.Target.Set(write.Name, useNewValue ? write.NewValue : write.OldValue);
            write.Target.MarkDirty();
        }
    }
}
