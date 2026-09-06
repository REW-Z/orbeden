using System;
using System.Collections.Generic;
using System.Globalization;

namespace Orbeden;

internal static partial class ManagedTypeMetadataCache
{
    //把单个宿主字段应用到活跃 Wrapper。
    internal static bool ApplyHostField(ScriptBehaviour script, IntPtr host, string name)
    {
        if (name == "enabled") return true;
        ManagedTypeMetadata metadata = Get(script.GetType());
        IReadOnlyDictionary<string, ManagedHostField> values = ScriptBehaviour.ReadHostFields(host);
        if (!metadata.Fields.TryGetValue(name, out ManagedFieldMetadata? field)
            || !values.TryGetValue(name, out ManagedHostField stored)
            || !TryReadHostValue(field, stored, out object? converted))
            return false;

        field.Setter(script, converted);
        return true;
    }

    //把新脚本字段的默认值补进空白宿主。
    internal static void WriteMissingHostFields(ScriptBehaviour script, IntPtr host)
    {
        IReadOnlyDictionary<string, ManagedHostField> stored = ScriptBehaviour.ReadHostFields(host);
        foreach (ManagedFieldMetadata field in Get(script.GetType()).Fields.Values)
        {
            if (field.Name == "enabled") continue;
            bool success = stored.TryGetValue(field.Name, out ManagedHostField existing)
                ? ScriptBehaviour.WriteHostField(host, field.Name, existing.TypeName, existing.Value, field.InspectorVisible)
                : WriteHostField(script, host, field);
            if (!success) throw new InvalidOperationException($"Cannot persist script field '{field.Name}'.");
        }
    }

    //把显式代理写入同步到原生宿主字段表。
    internal static bool WriteHostField(ScriptBehaviour script, IntPtr host, ManagedFieldMetadata field)
    {
        if (field.Name == "enabled") return true;
        if (field.Kind == InteropValueKind.EnsId && field.Getter(script) is EnsId ensId)
        {
            string key = ensId.IsNull ? string.Empty : Ens.FromId(ensId).Transform?.ResourceKey ?? string.Empty;
            return ScriptBehaviour.WriteHostField(host, field.Name, "EnsId", key, field.InspectorVisible);
        }
        if (field.Kind == InteropValueKind.Object)
        {
            Object? reference = field.Getter(script) as Object;
            string key = reference?.ResourceKey ?? string.Empty;
            if (key.StartsWith("orphan://", StringComparison.Ordinal)) return false;
            return ScriptBehaviour.WriteHostField(host, field.Name,
                GetSerializedTypeName(field.FieldType, field.Kind), key, field.InspectorVisible);
        }
        if (!TryToInterop(field.Getter(script), field.FieldType, out InteropValue value)) return false;
        return ScriptBehaviour.WriteHostField(host, field.Name,
            GetSerializedTypeName(field.FieldType, field.Kind),
            FormatSerialized(value),
            field.InspectorVisible);
    }

    //获取与 World Field 一致的类型名称。
    private static string GetSerializedTypeName(Type type, InteropValueKind kind) => kind switch
    {
        InteropValueKind.Bool => "bool",
        InteropValueKind.Int32 => "int32",
        InteropValueKind.UInt32 => "uint32",
        InteropValueKind.UInt64 => "uint64",
        InteropValueKind.Float32 => "float32",
        InteropValueKind.String => "string",
        InteropValueKind.Vector3 => "vector3",
        InteropValueKind.Color => "color",
        InteropValueKind.Quaternion => "quaternion",
        InteropValueKind.EnsId => "EnsId",
        InteropValueKind.Object => $"Ref<{type.FullName}>",
        _ => string.Empty,
    };

    //把互操作值格式化为 World Field 的稳定文本。
    private static string FormatSerialized(InteropValue value)
    {
        switch (value.Kind)
        {
            case InteropValueKind.Bool when value.TryGet(out bool boolean):
                return boolean ? "true" : "false";
            case InteropValueKind.Int32 when value.TryGet(out int integer):
                return integer.ToString(CultureInfo.InvariantCulture);
            case InteropValueKind.UInt32 when value.TryGet(out uint unsigned):
                return unsigned.ToString(CultureInfo.InvariantCulture);
            case InteropValueKind.UInt64 when value.TryGet(out ulong unsignedLong):
                return unsignedLong.ToString(CultureInfo.InvariantCulture);
            case InteropValueKind.Float32 when value.TryGet(out float number):
                return FormatFloat(number);
            case InteropValueKind.String when value.TryGet(out string text):
                return text;
            case InteropValueKind.Vector3 when value.TryGet(out vector3 vector):
                return string.Join(" ", FormatFloat(vector.x), FormatFloat(vector.y), FormatFloat(vector.z));
            case InteropValueKind.Color when value.TryGet(out color4 color):
                return string.Join(" ", FormatFloat(color.r), FormatFloat(color.g), FormatFloat(color.b), FormatFloat(color.a));
            case InteropValueKind.Quaternion when value.TryGet(out quaternion rotation):
                return string.Join(" ", FormatFloat(rotation.x), FormatFloat(rotation.y), FormatFloat(rotation.z), FormatFloat(rotation.w));
            case InteropValueKind.EnsId when value.TryGet(out EnsId ens):
                return $"{ens.id}:{ens.version}";
            case InteropValueKind.Object when value.TryGet(out int objectId):
                return objectId.ToString(CultureInfo.InvariantCulture);
            default:
                return string.Empty;
        }
    }

    private static string FormatFloat(float value) =>
        value.ToString("R", CultureInfo.InvariantCulture);

    /// <summary>区分持久化引用和只用于互操作的运行时 ObjectId。</summary>
    private static bool TryReadHostValue(ManagedFieldMetadata field, ManagedHostField stored, out object? result)
    {
        if (field.Kind == InteropValueKind.EnsId)
        {
            result = string.IsNullOrEmpty(stored.Value) ? EnsId.Null : Ens.Find(stored.Value).Id;
            return true;
        }
        if (field.Kind == InteropValueKind.Object)
        {
            result = ScriptBehaviour.ResolveReference(stored.Value, field.FieldType);
            return true;
        }
        result = null;
        return TryParseSerialized(field.Kind, stored.Value, out InteropValue value)
            && TryFromInterop(value, field.FieldType, out result);
    }
}
