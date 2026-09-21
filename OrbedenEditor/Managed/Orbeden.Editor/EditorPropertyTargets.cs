using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using Orbeden;

namespace OrbedenEditor;

internal static class EditorInteropValueText
{
    internal static bool TryParse(InteropValueKind kind, string text, out InteropValue value)
    {
        switch (kind)
        {
            case InteropValueKind.Bool:
                value = InteropValue.From(text is "true" or "1");
                return text is "true" or "false" or "1" or "0";
            case InteropValueKind.Int32 when int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int integer): value = InteropValue.From(integer); return true;
            case InteropValueKind.UInt32 when uint.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out uint unsigned): value = InteropValue.From(unsigned); return true;
            case InteropValueKind.UInt64 when ulong.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out ulong unsignedLong): value = InteropValue.From(unsignedLong); return true;
            case InteropValueKind.Float32 when float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float number): value = InteropValue.From(number); return true;
            case InteropValueKind.String: value = InteropValue.From(text); return true;
            case InteropValueKind.StringId: value = InteropValue.FromStringId(text); return true;
            case InteropValueKind.Object when int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int objectId): value = InteropValue.FromObjectId(objectId); return true;
            case InteropValueKind.Vector3 when TryFloats(text, 3, out float[] vector): value = InteropValue.From(new vector3(vector[0], vector[1], vector[2])); return true;
            case InteropValueKind.Color when TryFloats(text, 4, out float[] color): value = InteropValue.From(new color(color[0], color[1], color[2], color[3])); return true;
            case InteropValueKind.Quaternion when TryFloats(text, 4, out float[] rotation): value = InteropValue.From(new quaternion(rotation[0], rotation[1], rotation[2], rotation[3])); return true;
            case InteropValueKind.EnsId:
            {
                string[] parts = text.Split(':');
                if (parts.Length == 2 && uint.TryParse(parts[0], out uint id) && uint.TryParse(parts[1], out uint version))
                {
                    value = InteropValue.From(new EnsId(id, version));
                    return true;
                }
                break;
            }
        }
        value = default;
        return false;
    }

    internal static string Format(InteropValue value)
    {
        return value.Kind switch
        {
            InteropValueKind.Empty => string.Empty,
            InteropValueKind.Bool => value.TryGet(out bool boolean) && boolean ? "true" : "false",
            InteropValueKind.Int32 => value.TryGet(out int integer) ? integer.ToString(CultureInfo.InvariantCulture) : string.Empty,
            InteropValueKind.UInt32 => value.TryGet(out uint unsigned) ? unsigned.ToString(CultureInfo.InvariantCulture) : string.Empty,
            InteropValueKind.UInt64 => value.TryGet(out ulong unsignedLong) ? unsignedLong.ToString(CultureInfo.InvariantCulture) : string.Empty,
            InteropValueKind.Float32 => value.TryGet(out float number) ? number.ToString("R", CultureInfo.InvariantCulture) : string.Empty,
            InteropValueKind.String or InteropValueKind.StringId => value.TryGet(out string text) ? text : string.Empty,
            InteropValueKind.Vector3 => value.TryGet(out vector3 vector) ? Join(vector.x, vector.y, vector.z) : string.Empty,
            InteropValueKind.Color => value.TryGet(out color color) ? Join(color.r, color.g, color.b, color.a) : string.Empty,
            InteropValueKind.Quaternion => value.TryGet(out quaternion rotation) ? Join(rotation.x, rotation.y, rotation.z, rotation.w) : string.Empty,
            InteropValueKind.EnsId => value.TryGet(out EnsId ens) ? $"{ens.id}:{ens.version}" : string.Empty,
            InteropValueKind.Object => value.TryGet(out int objectId) ? objectId.ToString(CultureInfo.InvariantCulture) : string.Empty,
            _ => string.Empty,
        };
    }

    private static bool TryFloats(string text, int count, out float[] values)
    {
        string[] parts = text.Split([' ', ',', ';'], StringSplitOptions.RemoveEmptyEntries);
        values = new float[count];
        if (parts.Length != count) return false;
        for (int index = 0; index < count; ++index)
        {
            if (!float.TryParse(parts[index], NumberStyles.Float, CultureInfo.InvariantCulture, out values[index])) return false;
        }
        return true;
    }

    private static string Join(params float[] values) => string.Join(' ', values.Select(value => value.ToString("R", CultureInfo.InvariantCulture)));
}

internal sealed unsafe class NativeComponentPropertyTarget : IPropertyTarget
{
    private sealed class FieldSnapshot
    {
        internal PropertyDescriptor Descriptor;
        internal byte[] Name = [];
        internal byte[] ReferenceType = [];
        internal byte[] Text = [];
        internal EditorValueAbi Raw;
        internal InteropValue Value;
    }

    private string stableId = string.Empty;
    private byte[] stableIdBytes = [];
    private int objectId;
    private uint generation;
    private InteropStatus status = InteropStatus.NotFound;
    private readonly List<FieldSnapshot> fields = [];
    private readonly Dictionary<string, FieldSnapshot> fieldsByName = new(StringComparer.Ordinal);
    private readonly List<PropertyDescriptor> properties = [];
    private readonly Action dirty;
    public int PropertyVersion { get; private set; }

    /// <summary>保存运行时身份，延后到文档刷新时读取属性。</summary>
    internal NativeComponentPropertyTarget(NativeComponentInfo component, Action markDirty)
    {
        objectId = component.ObjectId;
        dirty = markDirty;
    }

    public bool AllowsSceneReferences => true;
    public string Identity => $"native:{stableId}";
    public IReadOnlyList<PropertyDescriptor> Properties => properties;

    /// <summary>批量刷新值，仅在字段结构变化时重建描述信息。</summary>
    public void Refresh()
    {
        status = EditorNativeComponents.ReadComponentSnapshot(objectId, out EditorComponentSnapshotAbi snapshot);
        if (status != InteropStatus.Ok || (stableId.Length != 0 && !snapshot.StableId.Bytes.SequenceEqual(stableIdBytes)))
        {
            //通过稳定身份跟随 Undo 恢复后的组件
            objectId = stableId.Length == 0 ? 0 : EditorNativeComponents.FindComponent(stableId);
            status = EditorNativeComponents.ReadComponentSnapshot(objectId, out snapshot);
        }
        if (status != InteropStatus.Ok)
        {
            if (properties.Count != 0) { properties.Clear(); fields.Clear(); fieldsByName.Clear(); ++PropertyVersion; }
            return;
        }
        if (stableId.Length == 0)
        {
            stableId = snapshot.StableId.ToString();
            stableIdBytes = snapshot.StableId.Bytes.ToArray();
        }

        //比较字段结构，不分配名称字符串
        bool changed = generation != snapshot.Generation || fields.Count != snapshot.Count;
        for (int index = 0; !changed && index < snapshot.Count; ++index)
        {
            ref EditorPropertyAbi source = ref snapshot.Properties[index];
            FieldSnapshot field = fields[index];
            changed = field.Descriptor.Kind != source.Value.Kind
                || !source.Name.Bytes.SequenceEqual(field.Name)
                || !source.ReferenceType.Bytes.SequenceEqual(field.ReferenceType);
        }
        if (changed)
        {
            fields.Clear(); fieldsByName.Clear(); properties.Clear();
            generation = snapshot.Generation;
            for (int index = 0; index < snapshot.Count; ++index)
            {
                ref EditorPropertyAbi source = ref snapshot.Properties[index];
                FieldSnapshot field = new()
                {
                    Descriptor = new(source.Name.ToString(), source.Value.Kind, source.ReferenceType.ToString()),
                    Name = source.Name.Bytes.ToArray(),
                    ReferenceType = source.ReferenceType.Bytes.ToArray(),
                };
                fields.Add(field);
                fieldsByName[field.Descriptor.Name] = field;
                properties.Add(field.Descriptor);
            }
            ++PropertyVersion;
        }

        //只转换发生变化的值，避免静态数值逐帧装箱
        for (int index = 0; index < snapshot.Count; ++index)
        {
            EditorValueAbi source = snapshot.Properties[index].Value;
            FieldSnapshot field = fields[index];
            bool valueChanged = changed || field.Raw.Status != source.Status;
            if (source.Kind is InteropValueKind.String or InteropValueKind.StringId)
            {
                EditorTextAbi text = *(EditorTextAbi*)source.Payload;
                if (!text.Bytes.SequenceEqual(field.Text)) { field.Text = text.Bytes.ToArray(); valueChanged = true; }
            }
            else
            {
                fixed (byte* previous = field.Raw.Payload)
                    valueChanged |= !new ReadOnlySpan<byte>(source.Payload, 16).SequenceEqual(new ReadOnlySpan<byte>(previous, 16));
            }
            if (valueChanged) field.Value = source.Status == InteropStatus.Ok ? source.Decode() : default;
            field.Raw = source;
            //托管缓存只保存已复制的文本，不保留原生文本指针
            if (source.Kind is InteropValueKind.String or InteropValueKind.StringId)
                fixed (byte* payload = field.Raw.Payload) new Span<byte>(payload, 16).Clear();
        }
    }

    /// <summary>读取本次文档刷新持有的属性快照。</summary>
    public InteropStatus TryGet(string name, out InteropValue value)
    {
        value = default;
        if (status != InteropStatus.Ok) return status;
        if (!fieldsByName.TryGetValue(name, out FieldSnapshot? field)) return InteropStatus.NotFound;
        value = field.Value;
        return field.Raw.Status;
    }

    /// <summary>验证字段仍存在且值类型一致。</summary>
    public InteropStatus Validate(string name, InteropValue value)
    {
        if (status != InteropStatus.Ok) return status;
        return fieldsByName.TryGetValue(name, out FieldSnapshot? field) && field.Descriptor.Kind == value.Kind
            ? InteropStatus.Ok : InteropStatus.TypeMismatch;
    }

    /// <summary>通过稳定身份和字段名执行写入。</summary>
    public InteropStatus Set(string name, InteropValue value)
    {
        if (Validate(name, value) != InteropStatus.Ok) return InteropStatus.TypeMismatch;
        int current = EditorNativeComponents.FindComponent(stableId);
        return EditorNativeComponents.SetComponentProperty(current, name, value);
    }

    /// <summary>标记属性所属场景已修改。</summary>
    public void MarkDirty() => dirty();
}

internal static class EditorManagedInteropValue
{
    internal static InteropValueKind GetKind(Type type)
    {
        Type valueType = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
        if (valueType == typeof(bool)) return InteropValueKind.Bool;
        if (valueType == typeof(int)) return InteropValueKind.Int32;
        if (valueType == typeof(uint)) return InteropValueKind.UInt32;
        if (valueType == typeof(ulong)) return InteropValueKind.UInt64;
        if (valueType == typeof(float)) return InteropValueKind.Float32;
        if (valueType == typeof(string)) return InteropValueKind.String;
        if (valueType == typeof(vector3)) return InteropValueKind.Vector3;
        if (valueType == typeof(color)) return InteropValueKind.Color;
        if (valueType == typeof(quaternion)) return InteropValueKind.Quaternion;
        if (valueType == typeof(EnsId)) return InteropValueKind.EnsId;
        if (typeof(Orbeden.Object).IsAssignableFrom(valueType)) return InteropValueKind.Object;
        return InteropValueKind.Empty;
    }

    internal static bool TryEncode(Type type, object? value, out InteropValue result)
    {
        Type valueType = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
        object? converted = type.IsEnum && value != null ? Convert.ChangeType(value, valueType, CultureInfo.InvariantCulture) : value;
        if (valueType == typeof(bool) && converted is bool boolean) result = InteropValue.From(boolean);
        else if (valueType == typeof(int) && converted is int integer) result = InteropValue.From(integer);
        else if (valueType == typeof(uint) && converted is uint unsigned) result = InteropValue.From(unsigned);
        else if (valueType == typeof(ulong) && converted is ulong unsignedLong) result = InteropValue.From(unsignedLong);
        else if (valueType == typeof(float) && converted is float number) result = InteropValue.From(number);
        else if (valueType == typeof(string)) result = InteropValue.From(converted as string);
        else if (valueType == typeof(vector3) && converted is vector3 vector) result = InteropValue.From(vector);
        else if (valueType == typeof(color) && converted is color color) result = InteropValue.From(color);
        else if (valueType == typeof(quaternion) && converted is quaternion rotation) result = InteropValue.From(rotation);
        else if (valueType == typeof(EnsId) && converted is EnsId ens) result = InteropValue.From(ens);
        else if (typeof(Orbeden.Object).IsAssignableFrom(valueType)) result = InteropValue.FromObject(converted as Orbeden.Object);
        else { result = default; return false; }
        return true;
    }

    internal static bool TryDecode(Type type, InteropValue value, out object? result)
    {
        Type valueType = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
        object? raw;
        if (valueType == typeof(bool) && value.TryGet(out bool boolean)) raw = boolean;
        else if (valueType == typeof(int) && value.TryGet(out int integer)) raw = integer;
        else if (valueType == typeof(uint) && value.TryGet(out uint unsigned)) raw = unsigned;
        else if (valueType == typeof(ulong) && value.TryGet(out ulong unsignedLong)) raw = unsignedLong;
        else if (valueType == typeof(float) && value.TryGet(out float number)) raw = number;
        else if (valueType == typeof(string) && value.TryGet(out string text)) raw = text;
        else if (valueType == typeof(vector3) && value.TryGet(out vector3 vector)) raw = vector;
        else if (valueType == typeof(color) && value.TryGet(out color color)) raw = color;
        else if (valueType == typeof(quaternion) && value.TryGet(out quaternion rotation)) raw = rotation;
        else if (valueType == typeof(EnsId) && value.TryGet(out EnsId ens)) raw = ens;
        else if (typeof(Orbeden.Object).IsAssignableFrom(valueType) && value.TryGet(out int objectId))
        {
            Orbeden.Object? objectValue = Orbeden.Object.FindLoadedObject(objectId);
            if (objectId != 0 && (objectValue == null || !valueType.IsInstanceOfType(objectValue)))
            {
                result = null;
                return false;
            }
            raw = objectValue;
        }
        else { result = null; return false; }

        result = type.IsEnum ? Enum.ToObject(type, raw!) : raw;
        return true;
    }
}

/// <summary>用一次缓存的托管反射元数据编辑运行态组件。</summary>
internal sealed class ManagedObjectPropertyTarget : IPropertyTarget
{
    private readonly object instance;
    private readonly Dictionary<string, FieldInfo> fields = new(StringComparer.Ordinal);
    private readonly Dictionary<string, PropertyInfo> managedProperties = new(StringComparer.Ordinal);
    private readonly IReadOnlyList<PropertyDescriptor> properties;
    private readonly Action dirty;
    private readonly bool hasEnabled;

    internal ManagedObjectPropertyTarget(object value, Action markDirty)
    {
        instance = value;
        dirty = markDirty;
        List<PropertyDescriptor> descriptors = [];
        hasEnabled = value is Script;
        if (hasEnabled) descriptors.Add(new PropertyDescriptor("enabled", InteropValueKind.Bool));

        List<Type> chain = [];
        for (Type? current = value.GetType(); current != null && current != typeof(Script) && current != typeof(Component); current = current.BaseType)
        {
            chain.Add(current);
        }
        chain.Reverse();
        foreach (Type type in chain)
        {
            foreach (FieldInfo field in type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (field.IsStatic || field.GetCustomAttribute<HideInEditorAttribute>() != null) continue;
                if (!field.IsPublic && field.GetCustomAttribute<SerializeFieldAttribute>() == null) continue;
                InteropValueKind kind = EditorManagedInteropValue.GetKind(field.FieldType);
                if (kind == InteropValueKind.Empty) continue;
                if (fields.ContainsKey(field.Name))
                {
                    fields[field.Name] = field;
                    continue;
                }
                fields[field.Name] = field;
                descriptors.Add(new PropertyDescriptor(field.Name, kind, kind is InteropValueKind.Object or InteropValueKind.EnsId ? field.FieldType.FullName ?? field.FieldType.Name : string.Empty));
            }
            foreach (PropertyInfo property in type.GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly))
            {
                if (property.GetIndexParameters().Length != 0 || property.GetMethod == null || property.SetMethod == null) continue;
                if (property.GetCustomAttribute<HideInEditorAttribute>() != null || fields.ContainsKey(property.Name)) continue;
                InteropValueKind kind = EditorManagedInteropValue.GetKind(property.PropertyType);
                if (kind == InteropValueKind.Empty) continue;
                if (managedProperties.ContainsKey(property.Name))
                {
                    managedProperties[property.Name] = property;
                    continue;
                }
                managedProperties[property.Name] = property;
                descriptors.Add(new PropertyDescriptor(property.Name, kind, kind is InteropValueKind.Object or InteropValueKind.EnsId ? property.PropertyType.FullName ?? property.PropertyType.Name : string.Empty));
            }
        }
        properties = descriptors;
    }

    public bool AllowsSceneReferences => instance is Component or Ens;
    public string Identity => $"managed:{RuntimeHelpers.GetHashCode(instance)}";
    public IReadOnlyList<PropertyDescriptor> Properties => properties;

    public InteropStatus TryGet(string name, out InteropValue value)
    {
        try
        {
            if (hasEnabled && name == "enabled")
            {
                value = InteropValue.From(((Script)instance).GetEnabled());
                return InteropStatus.Ok;
            }
            if (managedProperties.TryGetValue(name, out PropertyInfo? property))
            {
                return EditorManagedInteropValue.TryEncode(property.PropertyType, property.GetValue(instance), out value)
                    ? InteropStatus.Ok : InteropStatus.UnsupportedType;
            }
            if (!fields.TryGetValue(name, out FieldInfo? field))
            {
                value = default;
                return InteropStatus.NotFound;
            }
            return EditorManagedInteropValue.TryEncode(field.FieldType, field.GetValue(instance), out value)
                ? InteropStatus.Ok
                : InteropStatus.UnsupportedType;
        }
        catch
        {
            value = default;
            return InteropStatus.InvocationFailed;
        }
    }

    public InteropStatus Validate(string name, InteropValue value)
    {
        if (hasEnabled && name == "enabled") return value.Kind == InteropValueKind.Bool ? InteropStatus.Ok : InteropStatus.TypeMismatch;
        if (managedProperties.TryGetValue(name, out PropertyInfo? property))
        {
            return EditorManagedInteropValue.GetKind(property.PropertyType) == value.Kind
                && EditorManagedInteropValue.TryDecode(property.PropertyType, value, out _)
                ? InteropStatus.Ok : InteropStatus.TypeMismatch;
        }
        if (!fields.TryGetValue(name, out FieldInfo? field)) return InteropStatus.NotFound;
        return EditorManagedInteropValue.GetKind(field.FieldType) == value.Kind
            && EditorManagedInteropValue.TryDecode(field.FieldType, value, out _)
            ? InteropStatus.Ok
            : InteropStatus.TypeMismatch;
    }

    public InteropStatus Set(string name, InteropValue value)
    {
        try
        {
            if (Validate(name, value) != InteropStatus.Ok) return InteropStatus.TypeMismatch;
            if (hasEnabled && name == "enabled")
            {
                value.TryGet(out bool enabled);
                ((Script)instance).SetEnabled(enabled);
                return InteropStatus.Ok;
            }
            if (managedProperties.TryGetValue(name, out PropertyInfo? property))
            {
                if (!EditorManagedInteropValue.TryDecode(property.PropertyType, value, out object? propertyValue))
                    return InteropStatus.TypeMismatch;
                property.SetValue(instance, propertyValue);
                return InteropStatus.Ok;
            }
            FieldInfo field = fields[name];
            if (!EditorManagedInteropValue.TryDecode(field.FieldType, value, out object? decoded)) return InteropStatus.TypeMismatch;
            field.SetValue(instance, decoded);
            return InteropStatus.Ok;
        }
        catch
        {
            return InteropStatus.InvocationFailed;
        }
    }

    public void MarkDirty() => dirty();
}

/// <summary>把一个带业务 setter 的字段接入 PropertyDocument。</summary>
internal sealed class DelegatedPropertyTarget : IPropertyTarget
{
    private readonly string name;
    private readonly InteropValueKind kind;
    private readonly Func<InteropValue> getter;
    private readonly Func<InteropValue, InteropStatus> setter;
    private readonly Action dirty;
    private readonly IReadOnlyList<PropertyDescriptor> properties;

    internal DelegatedPropertyTarget(string identity, string propertyName, InteropValueKind propertyKind,
        Func<InteropValue> getValue, Func<InteropValue, InteropStatus> setValue, Action markDirty)
    {
        Identity = identity;
        name = propertyName;
        kind = propertyKind;
        getter = getValue;
        setter = setValue;
        dirty = markDirty;
        properties = [new PropertyDescriptor(name, kind)];
    }

    public string Identity { get; }
    public IReadOnlyList<PropertyDescriptor> Properties => properties;

    public InteropStatus TryGet(string propertyName, out InteropValue value)
    {
        if (!string.Equals(propertyName, name, StringComparison.Ordinal)) { value = default; return InteropStatus.NotFound; }
        value = getter();
        return value.Kind == kind ? InteropStatus.Ok : InteropStatus.TypeMismatch;
    }

    public InteropStatus Validate(string propertyName, InteropValue value) =>
        string.Equals(propertyName, name, StringComparison.Ordinal) && value.Kind == kind
            ? InteropStatus.Ok : InteropStatus.TypeMismatch;

    public InteropStatus Set(string propertyName, InteropValue value) =>
        Validate(propertyName, value) == InteropStatus.Ok ? setter(value) : InteropStatus.TypeMismatch;

    public void MarkDirty() => dirty();
}
