using Orbeden;

namespace OrbedenEditor;

internal readonly record struct PropertyDescriptor(string Name, InteropValueKind Kind, string ReferenceType = "", string Label = "", bool IsFixedSize = false);

internal interface IPropertyTarget
{
    bool AllowsSceneReferences => false;
    int PropertyVersion => 0;
    void Refresh() { }
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
        change.Undo();
        undo.RemoveAt(undo.Count - 1);
        redo.Add(change);
        EditorApplication.RequestRepaint();
        return true;
    }

    public static bool Redo()
    {
        if (redo.Count == 0) return false;
        EditorChange change = redo[^1];
        change.Redo();
        redo.RemoveAt(redo.Count - 1);
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
    public string ReferenceType { get; }
    //行的显示名，空表示直接用 Name；Name 是存取用的标识，可能不适合直接展示
    public string Label { get; }
    public bool IsFixedSize { get; }
    public bool AllowsSceneReferences => document.AllowsSceneReferences;
    public bool HasMultipleDifferentValues { get; internal set; }
    public InteropValue Value => value;
    internal bool Modified { get; private set; }
    internal bool IsReadable { get; set; } = true;

    internal PropertyValue(PropertyDocument owner, string name, InteropValueKind kind, string referenceType, string label, bool isFixedSize = false)
    {
        document = owner;
        Name = name;
        Kind = kind;
        ReferenceType = referenceType;
        Label = label;
        IsFixedSize = isFixedSize;
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

/// <summary>统一编辑 Edit/PIE World 中 C++ 与 C# 组件的事务文档。</summary>
public sealed class PropertyDocument
{
    private readonly IReadOnlyList<IPropertyTarget> targets;
    private readonly List<PropertyValue> properties = [];
    private bool modified;
    private readonly int[] targetVersions;
    private bool initialized;
    private int structureVersion;
    private int sortedVersion = -1;
    private string sortedType = string.Empty;
    private PropertyValue[] drawProperties = [];

    internal PropertyDocument(IReadOnlyList<IPropertyTarget> propertyTargets)
    {
        targets = propertyTargets;
        targetVersions = new int[targets.Count];
    }

    public IReadOnlyList<PropertyValue> Properties => properties;
    internal bool AllowsSceneReferences => targets.All(target => target.AllowsSceneReferences);
    public bool HasPendingChanges => modified;
    //列表字段的增删槽位要逐个目标操作：撤销得按组件各记各的槽位内容
    internal IReadOnlyList<IPropertyTarget> Targets => targets;

    /// <summary>批量刷新目标，仅在字段结构变化时重建公共属性。</summary>
    public void Update()
    {
        modified = false;
        bool rebuild = !initialized;
        for (int index = 0; index < targets.Count; ++index)
        {
            targets[index].Refresh();
            rebuild |= targetVersions[index] != targets[index].PropertyVersion;
            targetVersions[index] = targets[index].PropertyVersion;
        }
        if (rebuild)
        {
            properties.Clear();
            Dictionary<string, PropertyDescriptor> common = new(StringComparer.Ordinal);
            if (targets.Count != 0)
                foreach (PropertyDescriptor descriptor in targets[0].Properties) common[descriptor.Name] = descriptor;
            for (int index = 1; index < targets.Count; ++index)
            {
                Dictionary<string, PropertyDescriptor> current = new(StringComparer.Ordinal);
                foreach (PropertyDescriptor descriptor in targets[index].Properties) current[descriptor.Name] = descriptor;
                foreach (string name in common.Keys.ToArray())
                    if (!current.TryGetValue(name, out PropertyDescriptor descriptor) || descriptor != common[name]) common.Remove(name);
            }
            foreach ((string name, PropertyDescriptor descriptor) in common.OrderBy(pair => pair.Key, StringComparer.Ordinal))
                properties.Add(new PropertyValue(this, name, descriptor.Kind, descriptor.ReferenceType, descriptor.Label, descriptor.IsFixedSize));
            initialized = true;
            ++structureVersion;
        }

        //更新当前值与多选混合状态
        foreach (PropertyValue property in properties)
        {
            InteropValue first = default;
            bool mixed = false;
            bool readable = targets.Count != 0;
            for (int index = 0; index < targets.Count; ++index)
            {
                if (targets[index].TryGet(property.Name, out InteropValue current) != InteropStatus.Ok)
                {
                    readable = false;
                    break;
                }
                if (index == 0) first = current;
                else if (!first.Equals(current)) mixed = true;
            }
            property.IsReadable = readable;
            property.SetSnapshot(first, mixed);
        }
    }

    /// <summary>按组件布局缓存属性绘制顺序。</summary>
    internal IReadOnlyList<PropertyValue> GetDrawProperties(string nativeType)
    {
        if (sortedVersion == structureVersion && sortedType == nativeType) return drawProperties;
        string[] order = nativeType switch
        {
            //Ens 头部的激活勾选框排在名称上面
            "Ens" => ["LocalActive", "Name"],
            "Transform" => ["localPosition", "localRotation", "localScale"],
            //这些类型的 enabled 已经搬到卡片标题行，正文顺序表里不再列它
            "StaticMeshRenderer" => ["mesh", "materials", "drawQueue", "drawLayer", "castShadows", "receiveShadows"],
            "RigidBody" => ["bodyType", "mass", "useGravity", "linearDamping", "angularDamping", "linearVelocity", "angularVelocity", "continuousCollisionDetection", "lockFlags"],
            "CharacterController" => ["shape", "radius", "height", "halfExtents", "stepOffset", "contactOffset", "slopeLimit"],
            _ when nativeType.EndsWith("Collider", StringComparison.Ordinal) => ["isTrigger", "center", "halfExtents", "radius", "halfHeight", "mesh", "staticFriction", "dynamicFriction", "restitution", "collisionLayer", "collisionMask"],
            _ => [],
        };
        drawProperties = properties.OrderBy(property =>
        {
            int index = Array.IndexOf(order, property.Name);
            if (index >= 0) return index * 10;
            //列表元素排在它的容器行之后；容器不在顺序表里时两者一起落到末尾，靠名字顺序保持相邻
            int owner = Array.IndexOf(order, GetListFieldName(property.Name));
            return owner >= 0 ? owner * 10 + 5 : int.MaxValue;
        }).ThenBy(property => GetListElementIndex(property.Name)).ToArray();
        sortedVersion = structureVersion;
        sortedType = nativeType;
        return drawProperties;
    }

    //"字段名[下标]"取出字段名；不是列表元素时返回原名字
    private static string GetListFieldName(string name)
    {
        if (!name.EndsWith(']')) return name;
        int open = name.LastIndexOf('[');
        return open <= 0 ? name : name[..open];
    }

    //列表元素的下标；按数字排序，否则 [10] 会排在 [2] 前面。非元素条目为 0
    private static int GetListElementIndex(string name)
    {
        int open = name.LastIndexOf('[');
        if (open < 0 || !name.EndsWith(']')) return 0;
        return int.TryParse(name.AsSpan(open + 1, name.Length - open - 2), out int index) ? index : 0;
    }

    public PropertyValue? FindProperty(string name)
    {
        return properties.FirstOrDefault(property => string.Equals(property.Name, name, StringComparison.Ordinal));
    }

    public bool ApplyChanges(string undoLabel)
    {
        List<PropertyValue> changes = properties.Where(property => property.Modified).ToList();
        if (changes.Count == 0) return true;

        foreach (IPropertyTarget target in targets) target.Refresh();
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
        foreach (IPropertyTarget target in writes.Select(write => write.Target).Distinct()) target.Refresh();
        List<InteropValue> previous = [];
        foreach (var write in writes)
        {
            InteropValue value = useNewValue ? write.NewValue : write.OldValue;
            if (write.Target.Validate(write.Name, value) != InteropStatus.Ok
                || write.Target.TryGet(write.Name, out InteropValue current) != InteropStatus.Ok)
                throw new InvalidOperationException($"Cannot restore property {write.Name}.");
            previous.Add(current);
        }
        for (int index = 0; index < writes.Count; ++index)
        {
            var write = writes[index];
            if (write.Target.Set(write.Name, useNewValue ? write.NewValue : write.OldValue) == InteropStatus.Ok) continue;
            for (int rollback = index - 1; rollback >= 0; --rollback)
                writes[rollback].Target.Set(writes[rollback].Name, previous[rollback]);
            throw new InvalidOperationException($"Property restore failed: {write.Name}.");
        }
        foreach (IPropertyTarget target in writes.Select(write => write.Target).Distinct()) target.MarkDirty();
    }
}
