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
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetFieldCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int, byte*, int, int> GetFieldName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int, int> GetFieldKind;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int, byte*, int, int> GetFieldValue;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int, byte*, int, byte> SetFieldValue;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, byte*, int, byte*, int, byte, byte> SetManagedField;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetAddableTypeCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetAddableTypeName;
    public delegate* unmanaged[Cdecl]<IntPtr, uint, uint, byte*, int, byte, int> AddComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte> RemoveComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> CaptureComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, uint, uint, byte*, int, int, int> RestoreComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> FindComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, int, IntPtr*, IntPtr> GetHostBinding;
}
#pragma warning restore CS0649

/// <summary>原生反射字段分类，与 C++ Reflection::FieldKind 保持一致。</summary>
internal enum NativeFieldKind
{
    Unsupported,
    Bool,
    Int32,
    UInt32,
    UInt64,
    Float32,
    String,
    StringId,
    ObjectRef,
    Vector3,
    Color,
    Quaternion,
    EnsId,
}

/// <summary>一个无需 C# binding 的原生组件实例。</summary>
internal readonly record struct NativeComponentInfo(int ObjectId, string TypeName, bool IsManaged);

/// <summary>Editor 通用原生组件检查桥。</summary>
internal static unsafe class EditorNativeComponents
{
    private unsafe delegate int CopyText(byte* buffer, int size);

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

    /// <summary>获取组件可见字段数量。</summary>
    internal static int GetFieldCount(int objectId)
    {
        return api.GetFieldCount != null ? Math.Max(0, api.GetFieldCount(api.Context, objectId)) : 0;
    }

    /// <summary>获取字段名称。</summary>
    internal static string GetFieldName(int objectId, int fieldIndex)
    {
        return api.GetFieldName == null
            ? string.Empty
            : ReadText((byte* buffer, int size) => api.GetFieldName(api.Context, objectId, fieldIndex, buffer, size));
    }

    /// <summary>获取字段分类。</summary>
    internal static NativeFieldKind GetFieldKind(int objectId, int fieldIndex)
    {
        return api.GetFieldKind != null
            ? (NativeFieldKind)api.GetFieldKind(api.Context, objectId, fieldIndex)
            : NativeFieldKind.Unsupported;
    }

    /// <summary>获取字段序列化文本。</summary>
    internal static string GetFieldValue(int objectId, int fieldIndex)
    {
        return api.GetFieldValue == null
            ? string.Empty
            : ReadText((byte* buffer, int size) => api.GetFieldValue(api.Context, objectId, fieldIndex, buffer, size));
    }

    /// <summary>写入字段序列化文本。</summary>
    internal static bool SetFieldValue(int objectId, int fieldIndex, string value)
    {
        if (api.SetFieldValue == null) return false;
        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return api.SetFieldValue(api.Context, objectId, fieldIndex, pointer, bytes.Length) != 0;
        }
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
