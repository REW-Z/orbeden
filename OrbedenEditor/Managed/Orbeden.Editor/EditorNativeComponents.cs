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
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetFieldCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int, byte*, int, int> GetFieldName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int, int> GetFieldKind;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int, byte*, int, int> GetFieldValue;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int, byte*, int, byte> SetFieldValue;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetAddableTypeCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetAddableTypeName;
    public delegate* unmanaged[Cdecl]<IntPtr, uint, uint, byte*, int, int> AddComponent;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte> RemoveComponent;
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
internal readonly record struct NativeComponentInfo(int ObjectId, string TypeName);

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
            if (!string.IsNullOrEmpty(typeName)) result.Add(new NativeComponentInfo(objectId, typeName));
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
        if (api.AddComponent == null || string.IsNullOrEmpty(typeName)) return false;
        byte[] bytes = Encoding.UTF8.GetBytes(typeName);
        fixed (byte* pointer = bytes)
        {
            return api.AddComponent(api.Context, ens.id, ens.version, pointer, bytes.Length) != 0;
        }
    }

    /// <summary>按对象 ID 删除指定原生组件实例。</summary>
    internal static bool RemoveComponent(int objectId)
    {
        return api.RemoveComponent != null && api.RemoveComponent(api.Context, objectId) != 0;
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
