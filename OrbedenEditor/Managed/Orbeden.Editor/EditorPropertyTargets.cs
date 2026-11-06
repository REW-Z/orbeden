using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using Orbeden;

namespace OrbedenEditor;

internal static class EditorValueText
{
    internal static bool TryParse(NativeValueKind kind, string text, out object? value)
    {
        switch (kind)
        {
            case NativeValueKind.Bool:
                value = text is "true" or "1";
                return text is "true" or "false" or "1" or "0";
            case NativeValueKind.Int32 when int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int integer): value = integer; return true;
            case NativeValueKind.UInt32 when uint.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out uint unsigned): value = unsigned; return true;
            case NativeValueKind.UInt64 when ulong.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out ulong unsignedLong): value = unsignedLong; return true;
            case NativeValueKind.Float32 when float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float number): value = number; return true;
            case NativeValueKind.String: value = text; return true;
            case NativeValueKind.StringId: value = new StringId(text); return true;
            case NativeValueKind.Object when int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int objectId): value = NativeBindingRuntime.Wrap<Orbeden.Object>(objectId); return true;
            case NativeValueKind.Vector2 when TryFloats(text, 2, out float[] pair): value = new vector2(pair[0], pair[1]); return true;
            case NativeValueKind.Vector3 when TryFloats(text, 3, out float[] vector): value = new vector3(vector[0], vector[1], vector[2]); return true;
            case NativeValueKind.Color when TryFloats(text, 4, out float[] color): value = new color(color[0], color[1], color[2], color[3]); return true;
            case NativeValueKind.Quaternion when TryFloats(text, 4, out float[] rotation): value = new quaternion(rotation[0], rotation[1], rotation[2], rotation[3]); return true;
            case NativeValueKind.EnsId:
            {
                string[] parts = text.Split(':');
                if (parts.Length == 2 && uint.TryParse(parts[0], out uint id) && uint.TryParse(parts[1], out uint version))
                {
                    value = new EnsId(id, version);
                    return true;
                }
                break;
            }
        }
        value = default;
        return false;
    }

    internal static string Format(object? value)
    {
        return value.GetValueKind() switch
        {
            NativeValueKind.Empty => string.Empty,
            NativeValueKind.Bool => value.TryGet(out bool boolean) && boolean ? "true" : "false",
            NativeValueKind.Int32 => value.TryGet(out int integer) ? integer.ToString(CultureInfo.InvariantCulture) : string.Empty,
            NativeValueKind.UInt32 => value.TryGet(out uint unsigned) ? unsigned.ToString(CultureInfo.InvariantCulture) : string.Empty,
            NativeValueKind.UInt64 => value.TryGet(out ulong unsignedLong) ? unsignedLong.ToString(CultureInfo.InvariantCulture) : string.Empty,
            NativeValueKind.Float32 => value.TryGet(out float number) ? number.ToString("R", CultureInfo.InvariantCulture) : string.Empty,
            NativeValueKind.String or NativeValueKind.StringId => value.TryGet(out string text) ? text : string.Empty,
            NativeValueKind.Vector2 => value.TryGet(out vector2 pair) ? Join(pair.x, pair.y) : string.Empty,
            NativeValueKind.Vector3 => value.TryGet(out vector3 vector) ? Join(vector.x, vector.y, vector.z) : string.Empty,
            NativeValueKind.Color => value.TryGet(out color color) ? Join(color.r, color.g, color.b, color.a) : string.Empty,
            NativeValueKind.Quaternion => value.TryGet(out quaternion rotation) ? Join(rotation.x, rotation.y, rotation.z, rotation.w) : string.Empty,
            NativeValueKind.EnsId => value.TryGet(out EnsId ens) ? $"{ens.id}:{ens.version}" : string.Empty,
            NativeValueKind.Object => value.TryGet(out int objectId) ? objectId.ToString(CultureInfo.InvariantCulture) : string.Empty,
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

internal sealed class NativeComponentPropertyTarget : IPropertyTarget
{
    private readonly EditorNativeComponents.ComponentSnapshot snapshot = new();
    private int objectId;
    private InteropStatus status = InteropStatus.NotFound;
    private readonly Action dirty;
    //托管组件的类型名：枚举字段的序列化类型名是 uint32 这类基元名，刷新后要按它反查真正的枚举
    private readonly string managedTypeName;
    public int PropertyVersion => snapshot.PropertyVersion;

    /// <summary>保存运行时身份，延后到文档刷新时读取属性。</summary>
    internal NativeComponentPropertyTarget(NativeComponentInfo component, Action markDirty)
    {
        objectId = component.ObjectId;
        dirty = markDirty;
        managedTypeName = component.IsManaged ? component.TypeName : string.Empty;
    }

    public bool AllowsSceneReferences => true;
    public string Identity => $"native:{snapshot.StableId}";
    public IReadOnlyList<PropertyDescriptor> Properties => snapshot.Properties;

    /// <summary>刷新托管快照，跟随 Undo 恢复后的组件身份。</summary>
    public void Refresh()
    {
        status = EditorNativeComponents.ReadComponentSnapshot(objectId, snapshot);
        if (status == InteropStatus.NotFound && snapshot.StableId.Length != 0)
        {
            //重新定位撤销删除后恢复的组件
            //属性历史持有旧目标；恢复组件的 ObjectId 已改变，StableId 保持不变
            int restored = EditorNativeComponents.FindComponent(snapshot.StableId);
            if (restored != 0)
            {
                objectId = restored;
                status = EditorNativeComponents.ReadComponentSnapshot(objectId, snapshot);
            }
        }
        if (status != InteropStatus.Ok) snapshot.ClearProperties();
        else if (managedTypeName.Length != 0) AnnotateEnumFields();
    }

    //托管脚本字段只上报序列化类型名（枚举落到 uint32），这里按组件托管类型与字段名补回枚举的声明类型名，
    //Inspector 才能在快照重建时解析出下拉项。
    private void AnnotateEnumFields()
    {
        List<PropertyDescriptor> properties = snapshot.Properties;
        for (int index = 0; index < properties.Count; ++index)
        {
            PropertyDescriptor descriptor = properties[index];
            string enumName = EditorEnumOptions.ResolveFieldEnumName(managedTypeName, descriptor.Name);
            if (enumName.Length != 0) properties[index] = descriptor with { TypeName = enumName };
        }
    }

    /// <summary>读取本次文档刷新持有的属性快照。</summary>
    public InteropStatus TryGet(string name, out object? value)
    {
        value = default;
        if (status != InteropStatus.Ok) return status;
        if (!snapshot.FieldsByName.TryGetValue(name, out var field)) return InteropStatus.NotFound;
        value = field.Value;
        return field.Status;
    }

    /// <summary>验证字段仍存在且值类型一致。</summary>
    public InteropStatus Validate(string name, object? value)
    {
        if (status != InteropStatus.Ok) return status;
        return snapshot.FieldsByName.TryGetValue(name, out var field) && NativeValues.Matches(value, field.Descriptor.Kind)
            ? InteropStatus.Ok : InteropStatus.TypeMismatch;
    }

    /// <summary>通过稳定身份和字段名执行写入。</summary>
    public InteropStatus Set(string name, object? value)
    {
        InteropStatus validation = Validate(name, value);
        if (validation != InteropStatus.Ok) return validation;
        int current = EditorNativeComponents.FindComponent(snapshot.StableId);
        return EditorNativeComponents.SetComponentProperty(current, name, value, snapshot.FieldsByName[name].Descriptor.Kind);
    }

    /// <summary>标记属性所属场景已修改。</summary>
    public void MarkDirty() => dirty();
}

internal static class EditorManagedValue
{
    internal static NativeValueKind GetKind(Type type)
    {
        Type valueType = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
        if (valueType == typeof(bool)) return NativeValueKind.Bool;
        if (valueType == typeof(int)) return NativeValueKind.Int32;
        if (valueType == typeof(uint)) return NativeValueKind.UInt32;
        if (valueType == typeof(ulong)) return NativeValueKind.UInt64;
        if (valueType == typeof(float)) return NativeValueKind.Float32;
        if (valueType == typeof(string)) return NativeValueKind.String;
        if (valueType == typeof(StringId)) return NativeValueKind.StringId;
        if (valueType == typeof(vector2)) return NativeValueKind.Vector2;
        if (valueType == typeof(vector3)) return NativeValueKind.Vector3;
        if (valueType == typeof(color)) return NativeValueKind.Color;
        if (valueType == typeof(quaternion)) return NativeValueKind.Quaternion;
        if (valueType == typeof(EnsId)) return NativeValueKind.EnsId;
        if (typeof(Orbeden.Object).IsAssignableFrom(valueType)) return NativeValueKind.Object;
        return NativeValueKind.Empty;
    }

    internal static bool TryEncode(Type type, object? value, out object? result)
    {
        Type valueType = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
        object? converted = type.IsEnum && value != null ? Convert.ChangeType(value, valueType, CultureInfo.InvariantCulture) : value;
        if (valueType == typeof(bool) && converted is bool boolean) result = boolean;
        else if (valueType == typeof(int) && converted is int integer) result = integer;
        else if (valueType == typeof(uint) && converted is uint unsigned) result = unsigned;
        else if (valueType == typeof(ulong) && converted is ulong unsignedLong) result = unsignedLong;
        else if (valueType == typeof(float) && converted is float number) result = number;
        else if (valueType == typeof(string)) result = converted as string;
        else if (valueType == typeof(StringId) && converted is StringId key) result = key;
        else if (valueType == typeof(vector2) && converted is vector2 pair) result = pair;
        else if (valueType == typeof(vector3) && converted is vector3 vector) result = vector;
        else if (valueType == typeof(color) && converted is color color) result = color;
        else if (valueType == typeof(quaternion) && converted is quaternion rotation) result = rotation;
        else if (valueType == typeof(EnsId) && converted is EnsId ens) result = ens;
        else if (typeof(Orbeden.Object).IsAssignableFrom(valueType)) result = converted as Orbeden.Object;
        else { result = default; return false; }
        return true;
    }

    internal static bool TryDecode(Type type, object? value, out object? result)
    {
        Type valueType = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
        object? raw;
        if (valueType == typeof(bool) && value.TryGet(out bool boolean)) raw = boolean;
        else if (valueType == typeof(int) && value.TryGet(out int integer)) raw = integer;
        else if (valueType == typeof(uint) && value.TryGet(out uint unsigned)) raw = unsigned;
        else if (valueType == typeof(ulong) && value.TryGet(out ulong unsignedLong)) raw = unsignedLong;
        else if (valueType == typeof(float) && value.TryGet(out float number)) raw = number;
        else if (valueType == typeof(string) && value.TryGet(out string text)) raw = text;
        else if (valueType == typeof(StringId) && value is StringId key) raw = key;
        else if (valueType == typeof(vector2) && value.TryGet(out vector2 pair)) raw = pair;
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
        if (hasEnabled) descriptors.Add(new PropertyDescriptor("enabled", NativeValueKind.Bool));

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
                NativeValueKind kind = EditorManagedValue.GetKind(field.FieldType);
                if (kind == NativeValueKind.Empty) continue;
                if (fields.ContainsKey(field.Name))
                {
                    fields[field.Name] = field;
                    continue;
                }
                fields[field.Name] = field;
                //枚举字段带上枚举的全名，Inspector 据此画下拉框
                descriptors.Add(new PropertyDescriptor(field.Name, kind,
                    kind is NativeValueKind.Object or NativeValueKind.EnsId ? field.FieldType.FullName ?? field.FieldType.Name : string.Empty,
                    TypeName: field.FieldType.IsEnum ? field.FieldType.FullName ?? field.FieldType.Name : string.Empty));
            }
            foreach (PropertyInfo property in type.GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly))
            {
                if (property.GetIndexParameters().Length != 0 || property.GetMethod == null || property.SetMethod == null) continue;
                if (property.GetCustomAttribute<HideInEditorAttribute>() != null || fields.ContainsKey(property.Name)) continue;
                NativeValueKind kind = EditorManagedValue.GetKind(property.PropertyType);
                if (kind == NativeValueKind.Empty) continue;
                if (managedProperties.ContainsKey(property.Name))
                {
                    managedProperties[property.Name] = property;
                    continue;
                }
                managedProperties[property.Name] = property;
                descriptors.Add(new PropertyDescriptor(property.Name, kind,
                    kind is NativeValueKind.Object or NativeValueKind.EnsId ? property.PropertyType.FullName ?? property.PropertyType.Name : string.Empty,
                    TypeName: property.PropertyType.IsEnum ? property.PropertyType.FullName ?? property.PropertyType.Name : string.Empty));
            }
        }
        properties = descriptors;
    }

    public bool AllowsSceneReferences => instance is Component or Ens;
    public string Identity => $"managed:{RuntimeHelpers.GetHashCode(instance)}";
    public IReadOnlyList<PropertyDescriptor> Properties => properties;

    public InteropStatus TryGet(string name, out object? value)
    {
        try
        {
            if (hasEnabled && name == "enabled")
            {
                value = ((Script)instance).GetEnabled();
                return InteropStatus.Ok;
            }
            if (managedProperties.TryGetValue(name, out PropertyInfo? property))
            {
                return EditorManagedValue.TryEncode(property.PropertyType, property.GetValue(instance), out value)
                    ? InteropStatus.Ok : InteropStatus.UnsupportedType;
            }
            if (!fields.TryGetValue(name, out FieldInfo? field))
            {
                value = default;
                return InteropStatus.NotFound;
            }
            return EditorManagedValue.TryEncode(field.FieldType, field.GetValue(instance), out value)
                ? InteropStatus.Ok
                : InteropStatus.UnsupportedType;
        }
        catch
        {
            value = default;
            return InteropStatus.InvocationFailed;
        }
    }

    public InteropStatus Validate(string name, object? value)
    {
        if (hasEnabled && name == "enabled") return value.GetValueKind() == NativeValueKind.Bool ? InteropStatus.Ok : InteropStatus.TypeMismatch;
        if (managedProperties.TryGetValue(name, out PropertyInfo? property))
        {
            return EditorManagedValue.GetKind(property.PropertyType) == value.GetValueKind()
                && EditorManagedValue.TryDecode(property.PropertyType, value, out _)
                ? InteropStatus.Ok : InteropStatus.TypeMismatch;
        }
        if (!fields.TryGetValue(name, out FieldInfo? field)) return InteropStatus.NotFound;
        return EditorManagedValue.GetKind(field.FieldType) == value.GetValueKind()
            && EditorManagedValue.TryDecode(field.FieldType, value, out _)
            ? InteropStatus.Ok
            : InteropStatus.TypeMismatch;
    }

    public InteropStatus Set(string name, object? value)
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
                if (!EditorManagedValue.TryDecode(property.PropertyType, value, out object? propertyValue))
                    return InteropStatus.TypeMismatch;
                property.SetValue(instance, propertyValue);
                return InteropStatus.Ok;
            }
            FieldInfo field = fields[name];
            if (!EditorManagedValue.TryDecode(field.FieldType, value, out object? decoded)) return InteropStatus.TypeMismatch;
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

/// <summary>DelegatedPropertyTarget 上的一个属性：名字、取值类型，加一对读写委托。
/// ReferenceType 非空时该行走对象引用选择器，值按资源 Key 存取。</summary>
internal readonly record struct DelegatedProperty(
    string Name, NativeValueKind Kind, Func<object?> GetValue, Func<object?, InteropStatus> SetValue,
    string ReferenceType = "", string Label = "");

/// <summary>把一个带业务 setter 的字段接入 PropertyDocument；同一目标可以挂多个属性。</summary>
internal sealed class DelegatedPropertyTarget : IPropertyTarget
{
    private readonly Dictionary<string, DelegatedProperty> properties;
    private readonly List<PropertyDescriptor> descriptors;
    private readonly Action dirty;

    internal DelegatedPropertyTarget(string identity, string propertyName, NativeValueKind propertyKind,
        Func<object?> getValue, Func<object?, InteropStatus> setValue, Action markDirty)
        : this(identity, [new DelegatedProperty(propertyName, propertyKind, getValue, setValue)], markDirty)
    {
    }

    internal DelegatedPropertyTarget(string identity, IReadOnlyList<DelegatedProperty> values, Action markDirty)
    {
        Identity = identity;
        properties = values.ToDictionary(value => value.Name, StringComparer.Ordinal);
        descriptors = values.Select(value => new PropertyDescriptor(value.Name, value.Kind, value.ReferenceType, value.Label)).ToList();
        dirty = markDirty;
    }

    public string Identity { get; }
    public IReadOnlyList<PropertyDescriptor> Properties => descriptors;

    public InteropStatus TryGet(string propertyName, out object? value)
    {
        if (!properties.TryGetValue(propertyName, out DelegatedProperty property))
        {
            value = default;
            return InteropStatus.NotFound;
        }
        value = property.GetValue();
        return NativeValues.Matches(value, property.Kind) ? InteropStatus.Ok : InteropStatus.TypeMismatch;
    }

    public InteropStatus Validate(string propertyName, object? value) =>
        properties.TryGetValue(propertyName, out DelegatedProperty property) && NativeValues.Matches(value, property.Kind)
            ? InteropStatus.Ok : InteropStatus.TypeMismatch;

    public InteropStatus Set(string propertyName, object? value) =>
        Validate(propertyName, value) == InteropStatus.Ok
            ? properties[propertyName].SetValue(value) : InteropStatus.TypeMismatch;

    public void MarkDirty() => dirty();
}
