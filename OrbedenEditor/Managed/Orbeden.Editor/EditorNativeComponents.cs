using System.Runtime.InteropServices;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorComponentNativeApi
{
    public IntPtr Context;
    public delegate* unmanaged[Cdecl]<IntPtr, uint, uint, int> GetComponentCount;
    public delegate* unmanaged[Cdecl]<IntPtr, uint, uint, int, int> GetComponentObjectId;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetComponentTypeName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetComponentDomain;
    public delegate* unmanaged[Cdecl]<IntPtr, int, EditorComponentSnapshotAbi*, InteropStatus> ReadComponentSnapshot;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, EditorValueAbi*, InteropStatus> SetComponentProperty;
    public delegate* unmanaged[Cdecl]<IntPtr, uint> GetRegistryGeneration;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, byte*, int, byte*, int, byte, byte> SetManagedField;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetAddableTypeCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetAddableTypeName;
    public delegate* unmanaged[Cdecl]<IntPtr, uint, uint, byte*, int, byte, int> AddComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte> RemoveComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> CaptureComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, uint, uint, byte*, int, int, int> RestoreComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> FindComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, int, IntPtr*, IntPtr> GetHostBinding;
    public delegate* unmanaged[Cdecl]<IntPtr, EnsId*, int, int> GetWorldEns;
    public delegate* unmanaged[Cdecl]<IntPtr, EnsId, byte, void> SelectEns;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, byte> MatchComponentType;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, int> GetReferenceObjects;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, int> GetReferenceLabel;
    public delegate* unmanaged[Cdecl]<IntPtr, EnsId, EnsId, EnsId, byte, byte> MoveEns;
    public delegate* unmanaged[Cdecl]<IntPtr, EnsId, void> FocusEns;
}
#pragma warning restore CS0649

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorTextAbi
{
    public byte* Data;
    public int Length;
    public int Reserved;
    public readonly ReadOnlySpan<byte> Bytes => new(Data, Length);
    public override readonly string ToString() => Encoding.UTF8.GetString(Bytes);
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorValueAbi
{
    public InteropValueKind Kind;
    public InteropStatus Status;
    public fixed byte Payload[16];

    /// <summary>复制快照载荷到托管值。</summary>
    internal InteropValue Decode()
    {
        fixed (byte* pointer = Payload)
        {
            return Kind switch
            {
                InteropValueKind.Bool => InteropValue.From(*pointer != 0),
                InteropValueKind.Int32 => InteropValue.From(*(int*)pointer),
                InteropValueKind.UInt32 => InteropValue.From(*(uint*)pointer),
                InteropValueKind.UInt64 => InteropValue.From(*(ulong*)pointer),
                InteropValueKind.Float32 => InteropValue.From(*(float*)pointer),
                InteropValueKind.Vector3 => InteropValue.From(*(vector3*)pointer),
                InteropValueKind.Color => InteropValue.From(*(color*)pointer),
                InteropValueKind.Quaternion => InteropValue.From(*(quaternion*)pointer),
                InteropValueKind.EnsId => InteropValue.From(*(EnsId*)pointer),
                InteropValueKind.String => InteropValue.From(((EditorTextAbi*)pointer)->ToString()),
                InteropValueKind.StringId => InteropValue.FromStringId(((EditorTextAbi*)pointer)->ToString()),
                _ => default,
            };
        }
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct EditorPropertyAbi
{
    public EditorTextAbi Name;
    public EditorTextAbi ReferenceType;
    public EditorValueAbi Value;
    public InteropValueKind DeclaredKind;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorComponentSnapshotAbi
{
    public EditorTextAbi StableId;
    public EditorPropertyAbi* Properties;
    public int Count;
    public uint Generation;
}

/// <summary>一个无需 C# binding 的原生组件实例。</summary>
internal readonly record struct NativeComponentInfo(int ObjectId, string TypeName, bool IsManaged);

/// <summary>Editor 通用原生组件检查桥。</summary>
internal static unsafe class EditorNativeComponents
{
    private unsafe delegate int CopyText(byte* buffer, int size);

    /// <summary>完全由托管内存持有的属性快照。</summary>
    internal sealed class PropertySnapshot
    {
        internal PropertyDescriptor Descriptor;
        internal byte[] Name = [];
        internal byte[] ReferenceType = [];
        internal byte[] Text = [];
        internal ulong PayloadLow;
        internal ulong PayloadHigh;
        internal InteropStatus Status;
        internal InteropStatus SourceStatus;
        internal InteropValueKind SourceKind;
        internal InteropValue Value;
    }

    /// <summary>可复用的组件快照，不保存借用指针。</summary>
    internal sealed class ComponentSnapshot
    {
        internal string StableId = string.Empty;
        internal uint Generation;
        internal int PropertyVersion;
        internal readonly List<PropertySnapshot> Fields = [];
        internal readonly Dictionary<string, PropertySnapshot> FieldsByName = new(StringComparer.Ordinal);
        internal readonly List<PropertyDescriptor> Properties = [];

        /// <summary>清空失效属性，保留供 Undo 恢复使用的稳定身份。</summary>
        internal void ClearProperties()
        {
            if (Properties.Count == 0) return;
            Properties.Clear();
            Fields.Clear();
            FieldsByName.Clear();
            ++PropertyVersion;
        }
    }

    private static EditorComponentNativeApi api;

    /// <summary>保存原生组件检查 API。</summary>
    internal static void Initialize(EditorComponentNativeApi value)
    {
        api = value;
    }

    /// <summary>按组件挂载顺序枚举指定 Ens 的原生组件实例。</summary>
    internal static List<NativeComponentInfo> GetComponents(EnsId ens)
    {
        List<NativeComponentInfo> result = [];
        if (api.GetComponentCount == null || api.GetComponentObjectId == null || api.GetComponentTypeName == null) return result;

        int count = api.GetComponentCount(api.Context, ens.id, ens.version);
        result.Capacity = Math.Max(0, count);
        for (int index = 0; index < count; index++)
        {
            int objectId = api.GetComponentObjectId(api.Context, ens.id, ens.version, index);
            if (objectId == 0) continue;
            string typeName = ReadText((byte* buffer, int size) => api.GetComponentTypeName(api.Context, objectId, buffer, size));
            if (!string.IsNullOrEmpty(typeName)) result.Add(new NativeComponentInfo(objectId, typeName, api.GetComponentDomain(api.Context, objectId) != 0));
        }
        return result;
    }

    /// <summary>在借用期内复制组件属性，向调用方仅提供托管快照。</summary>
    internal static InteropStatus ReadComponentSnapshot(int objectId, ComponentSnapshot destination)
    {
        if (api.ReadComponentSnapshot == null) return InteropStatus.NotFound;
        EditorComponentSnapshotAbi snapshot = default;
        InteropStatus status = api.ReadComponentSnapshot(api.Context, objectId, &snapshot);
        if (status != InteropStatus.Ok) return status;
        //本方法复制完成前不调用其他原生接口，不向外暴露 ABI 指针或 Span
        if (destination.StableId.Length == 0) destination.StableId = snapshot.StableId.ToString();

        //比较字段结构，不分配名称字符串
        bool changed = destination.Generation != snapshot.Generation || destination.Fields.Count != snapshot.Count;
        for (int index = 0; !changed && index < snapshot.Count; ++index)
        {
            ref EditorPropertyAbi source = ref snapshot.Properties[index];
            PropertySnapshot field = destination.Fields[index];
            changed = field.Descriptor.Kind != source.DeclaredKind
                || !source.Name.Bytes.SequenceEqual(field.Name)
                || !source.ReferenceType.Bytes.SequenceEqual(field.ReferenceType);
        }
        if (changed)
        {
            destination.Fields.Clear();
            destination.FieldsByName.Clear();
            destination.Properties.Clear();
            destination.Generation = snapshot.Generation;
            for (int index = 0; index < snapshot.Count; ++index)
            {
                ref EditorPropertyAbi source = ref snapshot.Properties[index];
                PropertySnapshot field = new()
                {
                    Descriptor = new(source.Name.ToString(), source.DeclaredKind, source.ReferenceType.ToString()),
                    Name = source.Name.Bytes.ToArray(),
                    ReferenceType = source.ReferenceType.Bytes.ToArray(),
                };
                destination.Fields.Add(field);
                destination.FieldsByName[field.Descriptor.Name] = field;
                destination.Properties.Add(field.Descriptor);
            }
            ++destination.PropertyVersion;
        }

        //复制变化值，静态数值沿用已有装箱结果
        for (int index = 0; index < snapshot.Count; ++index)
        {
            EditorValueAbi source = snapshot.Properties[index].Value;
            PropertySnapshot field = destination.Fields[index];
            bool valueChanged = changed || field.SourceStatus != source.Status || field.SourceKind != source.Kind;
            if (source.Status == InteropStatus.Ok)
            {
                if (source.Kind is InteropValueKind.String or InteropValueKind.StringId)
                {
                    EditorTextAbi text = *(EditorTextAbi*)source.Payload;
                    if (!text.Bytes.SequenceEqual(field.Text))
                    {
                        field.Text = text.Bytes.ToArray();
                        valueChanged = true;
                    }
                }
                else
                {
                    ulong low = *(ulong*)source.Payload;
                    ulong high = *((ulong*)source.Payload + 1);
                    valueChanged |= field.PayloadLow != low || field.PayloadHigh != high;
                    field.PayloadLow = low;
                    field.PayloadHigh = high;
                }
            }
            if (valueChanged)
            {
                field.Status = source.Status;
                field.Value = default;
                if (source.Status == InteropStatus.Ok)
                {
                    //解析发生变化的宿主文本，并缓存解析失败状态
                    if (source.Kind == InteropValueKind.String && field.Descriptor.Kind != InteropValueKind.String)
                    {
                        string text = Encoding.UTF8.GetString(field.Text);
                        if (!EditorInteropValueText.TryParse(field.Descriptor.Kind, text, out field.Value))
                            field.Status = InteropStatus.InvocationFailed;
                    }
                    else if (source.Kind != field.Descriptor.Kind) field.Status = InteropStatus.TypeMismatch;
                    else field.Value = source.Decode();
                }
            }
            field.SourceStatus = source.Status;
            field.SourceKind = source.Kind;
        }
        return InteropStatus.Ok;
    }

    /// <summary>读取原生字段注册代次。</summary>
    internal static uint RegistryGeneration => api.GetRegistryGeneration == null ? 0 : api.GetRegistryGeneration(api.Context);

    /// <summary>按字段名写入类型化值。</summary>
    internal static InteropStatus SetComponentProperty(int objectId, string name, InteropValue value)
    {
        if (api.SetComponentProperty == null) return InteropStatus.NotFound;
        EditorValueAbi encoded = new() { Kind = value.Kind };
        byte[] nameBytes = Encoding.UTF8.GetBytes(name);
        byte[] text = value.Kind is InteropValueKind.String or InteropValueKind.StringId
            && value.TryGet(out string content) ? Encoding.UTF8.GetBytes(content) : [];
        fixed (byte* namePointer = nameBytes)
        fixed (byte* textPointer = text)
        {
            byte* pointer = encoded.Payload;
            switch (value.Kind)
            {
                case InteropValueKind.Bool: value.TryGet(out bool boolean); *pointer = boolean ? (byte)1 : (byte)0; break;
                case InteropValueKind.Int32: value.TryGet(out int integer); *(int*)pointer = integer; break;
                case InteropValueKind.UInt32: value.TryGet(out uint unsigned); *(uint*)pointer = unsigned; break;
                case InteropValueKind.UInt64: value.TryGet(out ulong wide); *(ulong*)pointer = wide; break;
                case InteropValueKind.Float32: value.TryGet(out float number); *(float*)pointer = number; break;
                case InteropValueKind.Vector3: value.TryGet(out vector3 vector); *(vector3*)pointer = vector; break;
                case InteropValueKind.Color: value.TryGet(out color color); *(color*)pointer = color; break;
                case InteropValueKind.Quaternion: value.TryGet(out quaternion rotation); *(quaternion*)pointer = rotation; break;
                case InteropValueKind.EnsId: value.TryGet(out EnsId ens); *(EnsId*)pointer = ens; break;
                case InteropValueKind.String: case InteropValueKind.StringId:
                    *(EditorTextAbi*)pointer = new EditorTextAbi { Data = textPointer, Length = text.Length }; break;
                default: return InteropStatus.UnsupportedType;
            }
            return api.SetComponentProperty(api.Context, objectId, namePointer, nameBytes.Length, &encoded);
        }
    }

    //读取符合声明类型的存活引用对象
    internal static string GetReferenceObjects(string type)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(type);
        fixed (byte* pointer = bytes)
        {
            int count = api.GetReferenceObjects(api.Context, pointer, bytes.Length, null, 0);
            byte[] output = new byte[count];
            fixed (byte* target = output) api.GetReferenceObjects(api.Context, pointer, bytes.Length, target, count);
            return Encoding.UTF8.GetString(output);
        }
    }

    //读取存活引用的显示名称
    internal static string GetReferenceLabel(string key)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(key);
        fixed (byte* pointer = bytes)
        {
            int count = api.GetReferenceLabel(api.Context, pointer, bytes.Length, null, 0);
            byte[] output = new byte[count];
            fixed (byte* target = output) api.GetReferenceLabel(api.Context, pointer, bytes.Length, target, count);
            return Encoding.UTF8.GetString(output);
        }
    }

    //枚举当前编辑 World 的 Ens
    internal static EnsId[] GetWorldEns()
    {
        int count = api.GetWorldEns(api.Context, null, 0);
        EnsId[] result = new EnsId[count];
        fixed (EnsId* pointer = result) api.GetWorldEns(api.Context, pointer, count);
        return result;
    }

    //定位场景引用所属 Ens
    internal static void SelectEns(EnsId ens, bool toggle = false) => api.SelectEns(api.Context, ens, toggle ? (byte)1 : (byte)0);

    //把场景相机聚焦到指定 Ens
    internal static void FocusEns(EnsId ens)
    {
        if (api.FocusEns != null) api.FocusEns(api.Context, ens);
    }

    //移动层级节点并设置变换保持规则
    internal static bool MoveEns(EnsId child, EnsId parent, EnsId before, bool preserveWorld)
        => api.MoveEns(api.Context, child, parent, before, preserveWorld ? (byte)1 : (byte)0) != 0;

    //按原生继承链匹配组件声明类型
    internal static bool MatchesComponentType(int objectId, string type)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(type);
        fixed (byte* pointer = bytes) return api.MatchComponentType(api.Context, objectId, pointer, bytes.Length) != 0;
    }

    /// <summary>枚举当前注册表中可创建的原生组件类型。</summary>
    internal static List<string> GetAddableTypes()
    {
        List<string> result = [];
        if (api.GetAddableTypeCount == null || api.GetAddableTypeName == null) return result;

        int count = Math.Max(0, api.GetAddableTypeCount(api.Context));
        result.Capacity = count;
        for (int index = 0; index < count; index++)
        {
            string typeName = ReadText((byte* buffer, int size) => api.GetAddableTypeName(api.Context, index, buffer, size));
            if (!string.IsNullOrEmpty(typeName)) result.Add(typeName);
        }
        return result;
    }

    /// <summary>按原生类型名新增组件。</summary>
    internal static bool AddComponent(EnsId ens, string typeName)
    {
        return AddComponentAndGetId(ens, typeName) != 0;
    }

    /// <summary>按类型名新增组件并返回其原生对象 ID。</summary>
    internal static int AddComponentAndGetId(EnsId ens, string typeName, bool isManaged = false)
    {
        if (api.AddComponent == null || string.IsNullOrEmpty(typeName)) return 0;
        byte[] bytes = Encoding.UTF8.GetBytes(typeName);
        fixed (byte* pointer = bytes)
        {
            return api.AddComponent(api.Context, ens.id, ens.version, pointer, bytes.Length, isManaged ? (byte)1 : (byte)0);
        }
    }

    /// <summary>新增或覆盖 C# 脚本宿主持有的序列化字段。</summary>
    internal static bool SetManagedField(int objectId, string name, string typeName, string value, bool inspectorVisible)
    {
        if (api.SetManagedField == null || string.IsNullOrEmpty(name) || string.IsNullOrEmpty(typeName)) return false;
        byte[] nameBytes = Encoding.UTF8.GetBytes(name);
        byte[] typeBytes = Encoding.UTF8.GetBytes(typeName);
        byte[] valueBytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* namePointer = nameBytes)
        fixed (byte* typePointer = typeBytes)
        fixed (byte* valuePointer = valueBytes)
        {
            return api.SetManagedField(api.Context,
                objectId,
                namePointer,
                nameBytes.Length,
                typePointer,
                typeBytes.Length,
                valuePointer,
                valueBytes.Length,
                inspectorVisible ? (byte)1 : (byte)0) != 0;
        }
    }

    /// <summary>按对象 ID 删除指定原生组件实例。</summary>
    internal static bool RemoveComponent(int objectId)
    {
        return api.RemoveComponent != null && api.RemoveComponent(api.Context, objectId) != 0;
    }

    /// <summary>捕获完整组件 XML，包括不可见字段和稳定身份。</summary>
    internal static string CaptureComponent(int objectId) => api.CaptureComponent == null ? string.Empty
        : ReadText((byte* buffer, int size) => api.CaptureComponent(api.Context, objectId, buffer, size));

    /// <summary>按快照恢复组件和挂载位置。</summary>
    internal static int RestoreComponent(EnsId ens, string snapshot, int index)
    {
        if (api.RestoreComponent == null) return 0;
        byte[] bytes = Encoding.UTF8.GetBytes(snapshot);
        fixed (byte* pointer = bytes)
            return api.RestoreComponent(api.Context, ens.id, ens.version, pointer, bytes.Length, index);
    }

    /// <summary>使用稳定身份重新定位经过 Undo 恢复的组件。</summary>
    internal static int FindComponent(string key)
    {
        if (api.FindComponent == null) return 0;
        byte[] bytes = Encoding.UTF8.GetBytes(key);
        fixed (byte* pointer = bytes) return api.FindComponent(api.Context, pointer, bytes.Length);
    }

    /// <summary>在真实宿主上构造字段默认值，不启动生命周期。</summary>
    internal static void InitializeManagedFields(EnsId ens, int objectId, Type type)
    {
        if (api.GetHostBinding == null) throw new InvalidOperationException("Script host binding is unavailable.");
        IntPtr host;
        IntPtr binding = api.GetHostBinding(api.Context, objectId, &host);
        GameScriptRuntime.InitializeEditorHost(binding, host, Ens.FromId(ens), type);
    }

    //用先查询长度再写入的 ABI 读取 UTF-8 文本。
    private static string ReadText(CopyText copy)
    {
        int required = copy(null, 0);
        if (required <= 0) return string.Empty;

        byte[] bytes = new byte[required];
        fixed (byte* pointer = bytes)
        {
            int actual = Math.Clamp(copy(pointer, bytes.Length), 0, bytes.Length);
            return Encoding.UTF8.GetString(bytes, 0, actual);
        }
    }
}
