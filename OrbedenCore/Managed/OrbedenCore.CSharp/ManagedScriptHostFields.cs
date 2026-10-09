using System;
using System.Collections.Generic;
using System.Globalization;
using System.Collections;
using System.Text;

namespace Orbeden;

internal static partial class ManagedTypeMetadataCache
{
    //把单个宿主字段应用到活跃 Wrapper。
    internal static bool ApplyHostField(Script script, IntPtr host, string name)
    {
        if (name == "enabled") return true;
        ManagedTypeMetadata metadata = Get(script.GetType());
        IReadOnlyDictionary<string, ManagedHostField> values = Script.ReadHostFields(host);
        if (!metadata.Fields.TryGetValue(name, out ManagedFieldMetadata? field)
            || !values.TryGetValue(name, out ManagedHostField stored)
            || !TryReadHostValue(script, field, stored, out object? converted))
            return false;

        field.Setter(script, converted);
        return true;
    }

    //把宿主字段表按脚本类型对账：类型里有而宿主里没有的补上（取构造函数里的默认值），
    //宿主里有而类型里没有的删掉。已有字段的值原样保留，值没变就不会标脏世界。
    //类型必须已经解析出来才允许走这条路：程序集加载失败或 Missing Script 时根本到不了这里，
    //所以那两种情况不会把字段删光。
    //返回被删掉的字段，供编辑器做撤销；运行时那两条路忽略返回值即可。
    internal static List<DroppedScriptField> SyncHostFields(Script script, IntPtr host)
    {
        ManagedTypeMetadata metadata = Get(script.GetType());
        IReadOnlyDictionary<string, ManagedHostField> stored = Script.ReadHostFields(host);
        List<DroppedScriptField> dropped = [];

        //先删后加：删除只针对类型不再声明的字段
        foreach ((string name, ManagedHostField field) in stored)
        {
            if (name == "enabled" || metadata.Fields.ContainsKey(name)) continue;
            if (!Script.RemoveHostField(host, name))
                throw new InvalidOperationException($"Cannot drop script field '{name}'.");
            dropped.Add(new DroppedScriptField(name, field.TypeName, field.Value));
        }

        foreach (ManagedFieldMetadata field in metadata.Fields.Values)
        {
            if (field.Name == "enabled") continue;
            bool success = stored.TryGetValue(field.Name, out ManagedHostField existing)
                ? Script.WriteHostField(host, field.Name, existing.TypeName, existing.Value, field.InspectorVisible)
                : WriteHostField(script, host, field);
            if (!success) throw new InvalidOperationException($"Cannot persist script field '{field.Name}'.");
        }

        return dropped;
    }

    //按脚本类型补齐宿主字段表里缺的字段，值取实例当前值。**不删除任何已有字段**：
    //Inspector 的补账路径在编辑态只拿得到"宿主已经有实例"的宿主，没有判定"表里多出字段"的立场，
    //删除留给运行时的 SyncHostFields 与撤销路径。
    internal static void EnsureHostFields(Script script, IntPtr host)
    {
        if (host == IntPtr.Zero) return;
        ManagedTypeMetadata metadata = Get(script.GetType());
        IReadOnlyDictionary<string, ManagedHostField> stored = Script.ReadHostFields(host);
        foreach (ManagedFieldMetadata field in metadata.Fields.Values)
        {
            if (field.Name == "enabled" || stored.ContainsKey(field.Name)) continue;
            if (!WriteHostField(script, host, field))
                throw new InvalidOperationException($"Cannot persist script field '{field.Name}'.");
        }
    }

    //字段事务结束后的通知接收者，由 ScriptRuntime 装配；为空表示没有运行态宿主。
    internal static Action<IntPtr>? HostFieldsChanged;

    [ThreadStatic] private static int fieldTransactionDepth;

    //按字段名列表原子应用宿主字段：先转换全部值，再统一赋值，任一步失败回滚到基线。
    //最外层事务结束才通知一次 FieldsChanged，嵌套调用不重复通知。
    internal static void ApplyHostFieldsTransaction(Script script, IntPtr host, IReadOnlyList<string> fieldNames)
    {
        if (host == IntPtr.Zero || fieldNames == null || fieldNames.Count == 0) return;
        ManagedTypeMetadata metadata = Get(script.GetType());
        IReadOnlyDictionary<string, ManagedHostField> stored = Script.ReadHostFields(host);

        List<ManagedFieldMetadata> fields = [];
        List<object?> baseline = [];
        List<object?> converted = [];
        foreach (string name in fieldNames)
        {
            if (!metadata.Fields.TryGetValue(name, out ManagedFieldMetadata? field) || field.Name == "enabled") continue;
            if (!stored.TryGetValue(name, out ManagedHostField value)) continue;
            if (!TryReadHostValue(script, field, value, out object? applied)) continue;
            fields.Add(field);
            baseline.Add(field.Getter(script));
            converted.Add(applied);
        }

        ++fieldTransactionDepth;
        try
        {
            int written = 0;
            try
            {
                for (; written < fields.Count; ++written) fields[written].Setter(script, converted[written]);
            }
            catch (Exception exception)
            {
                for (int index = written - 1; index >= 0; --index) fields[index].Setter(script, baseline[index]);
                Console.Error.WriteLine(
                    $"ScriptRuntime: field transaction on '{script.GetType().FullName}' rolled back. {exception}");
            }
        }
        finally
        {
            if (--fieldTransactionDepth == 0) HostFieldsChanged?.Invoke(host);
        }
    }

    //把当前 C# 值全部写回原生宿主字段表。原生字段表只是保存、复制、Prefab 与进入 Play
    //使用的快照，所以普通 setter 不逐次写它，改由这些边界各刷一次。
    internal static void FlushHostFields(Script script, IntPtr host)
    {
        if (host == IntPtr.Zero) return;
        ManagedTypeMetadata metadata = Get(script.GetType());
        foreach (ManagedFieldMetadata field in metadata.Fields.Values)
        {
            if (field.Name == "enabled") continue;
            if (!WriteHostField(script, host, field))
                Console.Error.WriteLine(
                    $"ScriptRuntime: flush '{script.GetType().FullName}.{field.Name}' failed.");
        }
    }

    //把显式代理写入同步到原生宿主字段表。
    //explicitEdit 为真表示用户刚编辑过该字段：此时空值就是清空引用，不再回填保留路径。
    internal static bool WriteHostField(Script script, IntPtr host, ManagedFieldMetadata field, bool explicitEdit = false)
    {
        if (field.Name == "enabled") return true;
        if (field.Kind == InteropValueKind.Array)
        {
            Type element = GetCollectionElementType(field.FieldType)!;
            TryGetKind(element, out var elementKind);
            List<string> values = [];
            if (field.Getter(script) is IEnumerable collection)
            {
                int index = 0;
                foreach (object? item in collection)
                {
                    if (elementKind == InteropValueKind.Object)
                    {
                        string key = (item as Object)?.ResourceKey ?? string.Empty;
                        if (key.Length == 0 && !explicitEdit) key = script.GetUnresolvedElement(field.Name, index) ?? string.Empty;
                        values.Add(key);
                    }
                    else if (elementKind == InteropValueKind.EnsId && item is EnsId id)
                    {
                        string key = id.IsNull ? string.Empty : Ens.FromId(id).ResourceKey ?? string.Empty;
                        if (key.Length == 0 && !explicitEdit) key = script.GetUnresolvedElement(field.Name, index) ?? string.Empty;
                        values.Add(key);
                    }
                    else if (TryToInterop(item, element, out var encoded)) values.Add(FormatSerialized(encoded));
                    else return false;
                    ++index;
                }
            }
            StringBuilder text = new(values.Count.ToString(CultureInfo.InvariantCulture) + ":");
            foreach (string itemText in values) text.Append(InteropText.GetUtf8ByteCount(itemText)).Append(':').Append(itemText);
            return Script.WriteHostField(host, field.Name, GetSerializedTypeName(field.FieldType, field.Kind), text.ToString(), field.InspectorVisible);
        }
        if (field.Kind == InteropValueKind.EnsId && field.Getter(script) is EnsId ensId)
        {
            string key = ensId.IsNull ? string.Empty : Ens.FromId(ensId).ResourceKey ?? string.Empty;
            //未解析的引用继续写回原路径，避免一次保存把目标抹成空。
            if (key.Length == 0 && !explicitEdit) key = script.GetUnresolvedReference(field.Name) ?? string.Empty;
            return Script.WriteHostField(host, field.Name, "EnsId", key, field.InspectorVisible);
        }
        if (field.Kind == InteropValueKind.Object)
        {
            Object? reference = field.Getter(script) as Object;
            string key = reference?.ResourceKey ?? string.Empty;
            if (key.StartsWith("orphan://", StringComparison.Ordinal)) return false;
            if (key.Length == 0 && !explicitEdit) key = script.GetUnresolvedReference(field.Name) ?? string.Empty;
            return Script.WriteHostField(host, field.Name,
                GetSerializedTypeName(field.FieldType, field.Kind), key, field.InspectorVisible);
        }
        if (!TryToInterop(field.Getter(script), field.FieldType, out InteropValue value)) return false;
        return Script.WriteHostField(host, field.Name,
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
        InteropValueKind.Vector2 => "vector2",
        InteropValueKind.Vector3 => "vector3",
        InteropValueKind.Color => "color",
        InteropValueKind.Quaternion => "quaternion",
        InteropValueKind.EnsId => "EnsId",
        InteropValueKind.Object => $"Ref<{type.FullName}>",
        InteropValueKind.Array => $"{(type.IsArray ? "Array" : "List")}<{GetCollectionSerializedElementType(type)}>",
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
            case InteropValueKind.Vector2 when value.TryGet(out vector2 pair):
                return string.Join(" ", FormatFloat(pair.x), FormatFloat(pair.y));
            case InteropValueKind.Vector3 when value.TryGet(out vector3 vector):
                return string.Join(" ", FormatFloat(vector.x), FormatFloat(vector.y), FormatFloat(vector.z));
            case InteropValueKind.Color when value.TryGet(out color color):
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
    private static bool TryReadHostValue(Script script, ManagedFieldMetadata field, ManagedHostField stored, out object? result)
    {
        if (field.Kind == InteropValueKind.Array)
        {
            result = null;
            Type element = GetCollectionElementType(field.FieldType)!;
            TryGetKind(element, out var kind);
            byte[] bytes = InteropText.EncodeUtf8(stored.Value);
            int position = 0;
            if (!ReadCollectionLength(bytes, ref position, out int count) || count > (bytes.Length - position) / 2) return false;
            Array array = Array.CreateInstance(element, count);
            for (int index = 0; index < count; ++index)
            {
                if (!ReadCollectionLength(bytes, ref position, out int length) || length > bytes.Length - position) return false;
                string text = InteropText.DecodeUtf8(bytes, position, length);
                position += length;
                object? itemValue;
                if (kind == InteropValueKind.Object)
                {
                    itemValue = Script.ResolveReference(text, element);
                    if (itemValue == null && text.Length != 0) script.RememberUnresolvedElement(field.Name, index, text);
                }
                else if (kind == InteropValueKind.EnsId)
                {
                    if (text.Length == 0) itemValue = EnsId.Null;
                    else
                    {
                        Ens target = Ens.Find(text);
                        itemValue = target.Id;
                        if (target.Id.IsNull) script.RememberUnresolvedElement(field.Name, index, text);
                    }
                }
                else if (!TryParseSerialized(kind, text, out var encoded) || !TryFromInterop(encoded, element, out itemValue)) return false;
                array.SetValue(itemValue, index);
            }
            if (position != bytes.Length) return false;
            if (field.FieldType.IsArray) result = array;
            else
            {
                IList list = (IList)Activator.CreateInstance(field.FieldType)!;
                foreach (object? itemValue in array) list.Add(itemValue);
                result = list;
            }
            return true;
        }
        if (field.Kind is InteropValueKind.EnsId or InteropValueKind.Object)
        {
            //每次加载都按当前世界重建保留表：解析成功即丢弃旧路径，失败则记下原路径。
            script.ClearUnresolvedReferences(field.Name);
            if (field.Kind == InteropValueKind.EnsId)
            {
                if (string.IsNullOrEmpty(stored.Value)) result = EnsId.Null;
                else
                {
                    Ens target = Ens.Find(stored.Value);
                    result = target.Id;
                    if (target.Id.IsNull) script.RememberUnresolvedReference(field.Name, stored.Value);
                }
                return true;
            }
            result = Script.ResolveReference(stored.Value, field.FieldType);
            if (result == null && !string.IsNullOrEmpty(stored.Value))
                script.RememberUnresolvedReference(field.Name, stored.Value);
            return true;
        }
        result = null;
        return TryParseSerialized(field.Kind, stored.Value, out InteropValue value)
            && TryFromInterop(value, field.FieldType, out result);
    }

    /// <summary>识别一维数组和标准 List 的元素类型。</summary>
    private static Type? GetCollectionElementType(Type type) => type.IsArray && type.GetArrayRank() == 1
        ? type.GetElementType() : type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>)
            ? type.GetGenericArguments()[0] : null;

    /// <summary>生成与原生元素分类一致的持久化类型名。</summary>
    private static string GetCollectionSerializedElementType(Type type)
    {
        Type element = GetCollectionElementType(type)!;
        TryGetKind(element, out var kind);
        return GetSerializedTypeName(element, kind);
    }

    /// <summary>读取非负十进制长度，拒绝溢出与不完整输入。</summary>
    private static bool ReadCollectionLength(byte[] bytes, ref int position, out int length)
    {
        length = 0;
        int begin = position;
        while (position < bytes.Length && bytes[position] != (byte)':')
        {
            int digit = bytes[position++] - (byte)'0';
            if (digit < 0 || digit > 9 || length > (int.MaxValue - digit) / 10) return false;
            length = length * 10 + digit;
        }
        return position > begin && position < bytes.Length && bytes[position++] == (byte)':';
    }
}
