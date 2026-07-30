using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ObjectBindApi
{
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetInstanceId;
    public delegate* unmanaged[Cdecl]<int, byte> IsAlive;
    public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> GetManagedWrapper;
    public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> SetManagedWrapper;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> Destroy;
    public delegate* unmanaged[Cdecl]<int*, int, uint> UnloadUnusedObjects;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ObjectExtensionBindApi
{
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetResourceKey;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct MeshBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, IntPtr> Load;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsValid;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetVertexCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetIndexCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetSubMeshCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetSubMeshName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, uint> GetSubMeshIndexStart;
    public delegate* unmanaged[Cdecl]<IntPtr, int, uint> GetSubMeshIndexCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, IntPtr> GetSubMeshMaterial;
    public delegate* unmanaged[Cdecl]<byte*, int, IntPtr> CreateInstance;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> SetName;
    public delegate* unmanaged[Cdecl]<IntPtr, ulong> GetRevision;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, int> GetVertexPositions;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, byte> SetVertexPositions;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, int> GetVertexNormals;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, byte> SetVertexNormals;
    public delegate* unmanaged[Cdecl]<IntPtr, vector2*, int, int> GetVertexTexcoords;
    public delegate* unmanaged[Cdecl]<IntPtr, vector2*, int, byte> SetVertexTexcoords;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, int> GetVertexTangents;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, byte> SetVertexTangents;
    public delegate* unmanaged[Cdecl]<IntPtr, uint*, int, int> GetIndexData;
    public delegate* unmanaged[Cdecl]<IntPtr, uint*, int, byte> SetIndexData;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> ClearGeometry;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> RefreshNormals;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte> ResizeSubMeshes;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, uint, uint, IntPtr, byte> ConfigureSubMesh;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct MaterialBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, IntPtr> Load;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsValid;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> GetShader;
    public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, byte> SetShader;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> HasTexture;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, int> GetTexture;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, byte> SetTexture;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> ClearTexture;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> HasColor;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, color4, color4> GetColor;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, color4, byte> SetColor;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> ClearColor;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> HasFloat;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, float, float> GetFloat;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, float, byte> SetFloat;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> ClearFloat;
    public delegate* unmanaged[Cdecl]<IntPtr, ulong> GetRevision;
    public delegate* unmanaged[Cdecl]<byte*, int, IntPtr, IntPtr> CreateInstance;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ShaderBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, IntPtr> Load;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsValid;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetVertexPath;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetFragmentPath;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetTextureSlotCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetTextureSlotName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetTextureSlotDisplayName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetTextureSlotDimension;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetColorSlotCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetColorSlotName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetColorSlotDisplayName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, color4> GetColorSlotDefault;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetFloatSlotCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetFloatSlotName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetFloatSlotDisplayName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, float> GetFloatSlotDefault;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetPassCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetPassName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetPassDepthTest;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetPassDepthWrite;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetPassBlend;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetPassCull;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, IntPtr> CreateFromSource;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, byte> ReplaceSource;
    public delegate* unmanaged[Cdecl]<IntPtr, ulong> GetRevision;
}
#pragma warning restore CS0649

internal static unsafe class ObjectBind
{
    private static ObjectBindApi api;
    private static ObjectExtensionBindApi extensionApi;
    private static bool initialized;

    //保存 C++ 传入的 Object 函数表
    internal static void Initialize(ObjectBindApi value, ObjectExtensionBindApi extensionValue)
    {
        api = value;
        extensionApi = extensionValue;
        initialized = api.GetInstanceId != null;
    }

    //读取原生对象 ID
    internal static int GetInstanceId(IntPtr pointer)
    {
        return initialized && pointer != IntPtr.Zero && api.GetInstanceId != null ? api.GetInstanceId(pointer) : 0;
    }

    //读取对象稳定资源 Key。
    internal static string GetResourceKey(IntPtr pointer)
    {
        if (!initialized || pointer == IntPtr.Zero || extensionApi.GetResourceKey == null) return string.Empty;

        int requiredBytes = extensionApi.GetResourceKey(pointer, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> bytes = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* output = bytes)
        {
            int actualBytes = extensionApi.GetResourceKey(pointer, output, requiredBytes);
            return Encoding.UTF8.GetString(bytes[..Math.Clamp(actualBytes, 0, requiredBytes)]);
        }
    }

    //判断原生对象是否存活
    internal static bool IsAlive(int instanceId)
    {
        return initialized && instanceId != 0 && api.IsAlive != null && api.IsAlive(instanceId) != 0;
    }

    //读取托管包装缓存
    internal static IntPtr GetManagedWrapper(IntPtr pointer)
    {
        return initialized && pointer != IntPtr.Zero && api.GetManagedWrapper != null ? api.GetManagedWrapper(pointer) : IntPtr.Zero;
    }

    //写入托管包装缓存
    internal static void SetManagedWrapper(IntPtr pointer, IntPtr handle)
    {
        if (initialized && pointer != IntPtr.Zero && api.SetManagedWrapper != null) api.SetManagedWrapper(pointer, handle);
    }

    //销毁原生对象
    internal static bool Destroy(IntPtr pointer)
    {
        return initialized && pointer != IntPtr.Zero && api.Destroy != null && api.Destroy(pointer) != 0;
    }

    //释放未使用对象
    internal static uint UnloadUnusedObjects(int[] roots)
    {
        if (!initialized || api.UnloadUnusedObjects == null) return 0;

        fixed (int* rootPointer = roots)
        {
            return api.UnloadUnusedObjects(rootPointer, roots.Length);
        }
    }
}

internal static unsafe class MeshBind
{
    private static MeshBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 Mesh 函数表
    internal static void Initialize(MeshBindApi value)
    {
        api = value;
        initialized = api.Load != null;
    }

    //加载 Mesh 资源
    internal static IntPtr Load(string key)
    {
        return initialized ? CallString(api.Load, key) : IntPtr.Zero;
    }

    //创建运行时 Mesh
    internal static IntPtr Create(string name)
    {
        return initialized ? CallString(api.CreateInstance, name) : IntPtr.Zero;
    }

    //判断 Mesh 是否有效
    internal static bool IsValid(IntPtr mesh)
    {
        return initialized && mesh != IntPtr.Zero && api.IsValid != null && api.IsValid(mesh) != 0;
    }

    //读取 Mesh 名称
    internal static string GetName(IntPtr mesh)
    {
        return initialized ? GetString(api.GetName, mesh) : string.Empty;
    }

    //写入 Mesh 名称
    internal static bool SetName(IntPtr mesh, string name)
    {
        if (!initialized || mesh == IntPtr.Zero || api.SetName == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(name ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return api.SetName(mesh, pointer, bytes.Length) != 0;
        }
    }

    //读取 Mesh 版本
    internal static ulong GetRevision(IntPtr mesh)
    {
        return initialized && mesh != IntPtr.Zero && api.GetRevision != null ? api.GetRevision(mesh) : 0;
    }

    //读取顶点数量
    internal static int GetVertexCount(IntPtr mesh) => initialized && mesh != IntPtr.Zero ? api.GetVertexCount(mesh) : 0;

    //读取索引数量
    internal static int GetIndexCount(IntPtr mesh) => initialized && mesh != IntPtr.Zero ? api.GetIndexCount(mesh) : 0;

    //读取子网格数量
    internal static int GetSubMeshCount(IntPtr mesh) => initialized && mesh != IntPtr.Zero ? api.GetSubMeshCount(mesh) : 0;

    //读取子网格名称
    internal static string GetSubMeshName(IntPtr mesh, int index) => initialized ? GetString(api.GetSubMeshName, mesh, index) : string.Empty;

    //读取子网格起始索引
    internal static uint GetSubMeshIndexStart(IntPtr mesh, int index) => initialized && mesh != IntPtr.Zero ? api.GetSubMeshIndexStart(mesh, index) : 0;

    //读取子网格索引数量
    internal static uint GetSubMeshIndexCount(IntPtr mesh, int index) => initialized && mesh != IntPtr.Zero ? api.GetSubMeshIndexCount(mesh, index) : 0;

    //读取子网格材质
    internal static IntPtr GetSubMeshMaterial(IntPtr mesh, int index)
    {
        return initialized && mesh != IntPtr.Zero && api.GetSubMeshMaterial != null ? api.GetSubMeshMaterial(mesh, index) : IntPtr.Zero;
    }

    //读取顶点位置数组
    internal static vector3[] GetVertexPositions(IntPtr mesh) => GetVector3Array(api.GetVertexPositions, mesh);

    //读取顶点法线数组
    internal static vector3[] GetVertexNormals(IntPtr mesh) => GetVector3Array(api.GetVertexNormals, mesh);

    //读取顶点 UV 数组
    internal static vector2[] GetVertexTexcoords(IntPtr mesh) => GetVector2Array(api.GetVertexTexcoords, mesh);

    //读取顶点切线数组
    internal static vector3[] GetVertexTangents(IntPtr mesh) => GetVector3Array(api.GetVertexTangents, mesh);

    //读取索引数组
    internal static uint[] GetIndexData(IntPtr mesh) => GetUIntArray(api.GetIndexData, mesh);

    //写入顶点位置数组
    internal static bool SetVertexPositions(IntPtr mesh, vector3[]? values) => SetVector3Array(api.SetVertexPositions, mesh, values);

    //写入顶点法线数组
    internal static bool SetVertexNormals(IntPtr mesh, vector3[]? values) => SetVector3Array(api.SetVertexNormals, mesh, values);

    //写入顶点 UV 数组
    internal static bool SetVertexTexcoords(IntPtr mesh, vector2[]? values) => SetVector2Array(api.SetVertexTexcoords, mesh, values);

    //写入顶点切线数组
    internal static bool SetVertexTangents(IntPtr mesh, vector3[]? values) => SetVector3Array(api.SetVertexTangents, mesh, values);

    //写入索引数组
    internal static bool SetIndexData(IntPtr mesh, uint[]? values) => SetUIntArray(api.SetIndexData, mesh, values);

    //清空几何数据
    internal static bool ClearGeometry(IntPtr mesh) => initialized && mesh != IntPtr.Zero && api.ClearGeometry(mesh) != 0;

    //重新计算法线
    internal static bool RefreshNormals(IntPtr mesh) => initialized && mesh != IntPtr.Zero && api.RefreshNormals(mesh) != 0;

    //调整子网格数量
    internal static bool ResizeSubMeshes(IntPtr mesh, int count) => initialized && mesh != IntPtr.Zero && api.ResizeSubMeshes(mesh, count) != 0;

    //配置子网格
    internal static bool ConfigureSubMesh(IntPtr mesh, int index, string name, uint indexStart, uint indexCount, IntPtr material)
    {
        if (!initialized || mesh == IntPtr.Zero || api.ConfigureSubMesh == null) return false;

        byte[] nameBytes = Encoding.UTF8.GetBytes(name ?? string.Empty);
        fixed (byte* namePointer = nameBytes)
        {
            return api.ConfigureSubMesh(mesh, index, namePointer, nameBytes.Length, indexStart, indexCount, material) != 0;
        }
    }

    //调用字符串输入并返回指针
    private static IntPtr CallString(delegate* unmanaged[Cdecl]<byte*, int, IntPtr> function, string? value)
    {
        if (function == null) return IntPtr.Zero;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length);
        }
    }

    //读取对象字符串
    private static string GetString(delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> function, IntPtr pointer)
    {
        if (function == null || pointer == IntPtr.Zero) return string.Empty;

        int requiredBytes = function(pointer, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = function(pointer, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return Encoding.UTF8.GetString(output[..length]);
        }
    }

    //读取对象索引字符串
    private static string GetString(delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> function, IntPtr pointer, int index)
    {
        if (function == null || pointer == IntPtr.Zero) return string.Empty;

        int requiredBytes = function(pointer, index, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = function(pointer, index, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return Encoding.UTF8.GetString(output[..length]);
        }
    }

    //读取 vector3 数组
    private static vector3[] GetVector3Array(delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, int> function, IntPtr mesh)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return [];

        int count = function(mesh, null, 0);
        if (count <= 0) return [];

        vector3[] values = new vector3[count];
        fixed (vector3* pointer = values)
        {
            int actualCount = function(mesh, pointer, values.Length);
            if (actualCount == values.Length) return values;
            Array.Resize(ref values, Math.Clamp(actualCount, 0, values.Length));
            return values;
        }
    }

    //读取 vector2 数组
    private static vector2[] GetVector2Array(delegate* unmanaged[Cdecl]<IntPtr, vector2*, int, int> function, IntPtr mesh)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return [];

        int count = function(mesh, null, 0);
        if (count <= 0) return [];

        vector2[] values = new vector2[count];
        fixed (vector2* pointer = values)
        {
            int actualCount = function(mesh, pointer, values.Length);
            if (actualCount == values.Length) return values;
            Array.Resize(ref values, Math.Clamp(actualCount, 0, values.Length));
            return values;
        }
    }

    //读取 uint 数组
    private static uint[] GetUIntArray(delegate* unmanaged[Cdecl]<IntPtr, uint*, int, int> function, IntPtr mesh)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return [];

        int count = function(mesh, null, 0);
        if (count <= 0) return [];

        uint[] values = new uint[count];
        fixed (uint* pointer = values)
        {
            int actualCount = function(mesh, pointer, values.Length);
            if (actualCount == values.Length) return values;
            Array.Resize(ref values, Math.Clamp(actualCount, 0, values.Length));
            return values;
        }
    }

    //写入 vector3 数组
    private static bool SetVector3Array(delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, byte> function, IntPtr mesh, vector3[]? values)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return false;

        fixed (vector3* pointer = values)
        {
            return function(mesh, pointer, values?.Length ?? 0) != 0;
        }
    }

    //写入 vector2 数组
    private static bool SetVector2Array(delegate* unmanaged[Cdecl]<IntPtr, vector2*, int, byte> function, IntPtr mesh, vector2[]? values)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return false;

        fixed (vector2* pointer = values)
        {
            return function(mesh, pointer, values?.Length ?? 0) != 0;
        }
    }

    //写入 uint 数组
    private static bool SetUIntArray(delegate* unmanaged[Cdecl]<IntPtr, uint*, int, byte> function, IntPtr mesh, uint[]? values)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return false;

        fixed (uint* pointer = values)
        {
            return function(mesh, pointer, values?.Length ?? 0) != 0;
        }
    }
}

internal static unsafe class MaterialBind
{
    private static MaterialBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 Material 函数表
    internal static void Initialize(MaterialBindApi value)
    {
        api = value;
        initialized = api.Load != null;
    }

    //加载 Material 资源
    internal static IntPtr Load(string key) => initialized ? CallString(api.Load, key) : IntPtr.Zero;

    //创建运行时 Material
    internal static IntPtr Create(string name, IntPtr shader) => initialized ? CallStringPointer(api.CreateInstance, name, shader) : IntPtr.Zero;

    //判断 Material 是否有效
    internal static bool IsValid(IntPtr material) => initialized && material != IntPtr.Zero && api.IsValid(material) != 0;

    //读取 Material 名称
    internal static string GetName(IntPtr material) => initialized ? GetString(api.GetName, material) : string.Empty;

    //读取材质版本
    internal static ulong GetRevision(IntPtr material) => initialized && material != IntPtr.Zero ? api.GetRevision(material) : 0;

    //读取材质 Shader
    internal static IntPtr GetShader(IntPtr material) => initialized && material != IntPtr.Zero ? api.GetShader(material) : IntPtr.Zero;

    //设置材质 Shader
    internal static bool SetShader(IntPtr material, IntPtr shader) => initialized && material != IntPtr.Zero && api.SetShader(material, shader) != 0;

    //判断纹理槽是否存在
    internal static bool HasTexture(IntPtr material, string slotName) => CallSlotBool(api.HasTexture, material, slotName);

    //读取纹理资源 Key
    internal static string GetTexture(IntPtr material, string slotName) => GetSlotString(api.GetTexture, material, slotName);

    //写入纹理资源 Key
    internal static bool SetTexture(IntPtr material, string slotName, string textureKey)
    {
        if (!initialized || material == IntPtr.Zero || api.SetTexture == null) return false;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        byte[] keyBytes = Encoding.UTF8.GetBytes(textureKey ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        fixed (byte* keyPointer = keyBytes)
        {
            return api.SetTexture(material, slotPointer, slotBytes.Length, keyPointer, keyBytes.Length) != 0;
        }
    }

    //清除纹理槽
    internal static bool ClearTexture(IntPtr material, string slotName) => CallSlotBool(api.ClearTexture, material, slotName);

    //判断颜色槽是否存在
    internal static bool HasColor(IntPtr material, string slotName) => CallSlotBool(api.HasColor, material, slotName);

    //读取颜色槽
    internal static color4 GetColor(IntPtr material, string slotName, color4 defaultValue)
    {
        if (!initialized || material == IntPtr.Zero || api.GetColor == null) return defaultValue;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            return api.GetColor(material, slotPointer, slotBytes.Length, defaultValue);
        }
    }

    //写入颜色槽
    internal static bool SetColor(IntPtr material, string slotName, color4 value)
    {
        if (!initialized || material == IntPtr.Zero || api.SetColor == null) return false;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            return api.SetColor(material, slotPointer, slotBytes.Length, value) != 0;
        }
    }

    //清除颜色槽
    internal static bool ClearColor(IntPtr material, string slotName) => CallSlotBool(api.ClearColor, material, slotName);

    //判断浮点槽是否存在
    internal static bool HasFloat(IntPtr material, string slotName) => CallSlotBool(api.HasFloat, material, slotName);

    //读取浮点槽
    internal static float GetFloat(IntPtr material, string slotName, float defaultValue)
    {
        if (!initialized || material == IntPtr.Zero || api.GetFloat == null) return defaultValue;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            return api.GetFloat(material, slotPointer, slotBytes.Length, defaultValue);
        }
    }

    //写入浮点槽
    internal static bool SetFloat(IntPtr material, string slotName, float value)
    {
        if (!initialized || material == IntPtr.Zero || api.SetFloat == null) return false;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            return api.SetFloat(material, slotPointer, slotBytes.Length, value) != 0;
        }
    }

    //清除浮点槽
    internal static bool ClearFloat(IntPtr material, string slotName) => CallSlotBool(api.ClearFloat, material, slotName);

    //调用字符串输入并返回指针
    private static IntPtr CallString(delegate* unmanaged[Cdecl]<byte*, int, IntPtr> function, string? value)
    {
        if (function == null) return IntPtr.Zero;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length);
        }
    }

    //调用字符串和指针输入并返回指针
    private static IntPtr CallStringPointer(delegate* unmanaged[Cdecl]<byte*, int, IntPtr, IntPtr> function, string? value, IntPtr pointerValue)
    {
        if (function == null) return IntPtr.Zero;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length, pointerValue);
        }
    }

    //读取对象字符串
    private static string GetString(delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> function, IntPtr pointer)
    {
        if (function == null || pointer == IntPtr.Zero) return string.Empty;

        int requiredBytes = function(pointer, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = function(pointer, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return Encoding.UTF8.GetString(output[..length]);
        }
    }

    //调用槽位 bool 函数
    private static bool CallSlotBool(delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> function, IntPtr material, string slotName)
    {
        if (!initialized || material == IntPtr.Zero || function == null) return false;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            return function(material, slotPointer, slotBytes.Length) != 0;
        }
    }

    //读取槽位字符串
    private static string GetSlotString(delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, int> function, IntPtr material, string slotName)
    {
        if (!initialized || material == IntPtr.Zero || function == null) return string.Empty;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            int requiredBytes = function(material, slotPointer, slotBytes.Length, null, 0);
            if (requiredBytes <= 0) return string.Empty;

            Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
            fixed (byte* outputPointer = output)
            {
                int actualBytes = function(material, slotPointer, slotBytes.Length, outputPointer, output.Length);
                int length = Math.Clamp(actualBytes, 0, output.Length);
                return Encoding.UTF8.GetString(output[..length]);
            }
        }
    }
}

internal static unsafe class ShaderBind
{
    private static ShaderBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 Shader 函数表
    internal static void Initialize(ShaderBindApi value)
    {
        api = value;
        initialized = api.Load != null;
    }

    //加载 Shader 资源
    internal static IntPtr Load(string key) => initialized ? CallString(api.Load, key) : IntPtr.Zero;

    //创建运行时 Shader
    internal static IntPtr CreateFromSource(string name, string vertexSource, string fragmentSource)
    {
        if (!initialized || api.CreateFromSource == null) return IntPtr.Zero;

        byte[] nameBytes = Encoding.UTF8.GetBytes(name ?? string.Empty);
        byte[] vertexBytes = Encoding.UTF8.GetBytes(vertexSource ?? string.Empty);
        byte[] fragmentBytes = Encoding.UTF8.GetBytes(fragmentSource ?? string.Empty);
        fixed (byte* namePointer = nameBytes)
        fixed (byte* vertexPointer = vertexBytes)
        fixed (byte* fragmentPointer = fragmentBytes)
        {
            return api.CreateFromSource(namePointer, nameBytes.Length, vertexPointer, vertexBytes.Length, fragmentPointer, fragmentBytes.Length);
        }
    }

    //判断 Shader 是否有效
    internal static bool IsValid(IntPtr shader) => initialized && shader != IntPtr.Zero && api.IsValid(shader) != 0;

    //读取 Shader 名称
    internal static string GetName(IntPtr shader) => initialized ? GetString(api.GetName, shader) : string.Empty;

    //读取 Shader 版本
    internal static ulong GetRevision(IntPtr shader) => initialized && shader != IntPtr.Zero ? api.GetRevision(shader) : 0;

    //读取顶点源码路径
    internal static string GetVertexPath(IntPtr shader) => initialized ? GetString(api.GetVertexPath, shader) : string.Empty;

    //读取片元源码路径
    internal static string GetFragmentPath(IntPtr shader) => initialized ? GetString(api.GetFragmentPath, shader) : string.Empty;

    //读取纹理槽数量
    internal static int GetTextureSlotCount(IntPtr shader) => initialized && shader != IntPtr.Zero ? api.GetTextureSlotCount(shader) : 0;

    //读取颜色槽数量
    internal static int GetColorSlotCount(IntPtr shader) => initialized && shader != IntPtr.Zero ? api.GetColorSlotCount(shader) : 0;

    //读取浮点槽数量
    internal static int GetFloatSlotCount(IntPtr shader) => initialized && shader != IntPtr.Zero ? api.GetFloatSlotCount(shader) : 0;

    //读取 Pass 数量
    internal static int GetPassCount(IntPtr shader) => initialized && shader != IntPtr.Zero ? api.GetPassCount(shader) : 0;

    //读取 Pass 名称
    internal static string GetPassName(IntPtr shader, int index) => initialized ? GetString(api.GetPassName, shader, index) : string.Empty;

    //读取 Pass 深度测试状态
    internal static int GetPassDepthTest(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetPassDepthTest(shader, index) : 0;

    //读取 Pass 深度写入状态
    internal static int GetPassDepthWrite(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetPassDepthWrite(shader, index) : 0;

    //读取 Pass 混合状态
    internal static int GetPassBlend(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetPassBlend(shader, index) : 0;

    //读取 Pass 剔除状态
    internal static int GetPassCull(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetPassCull(shader, index) : 0;

    //读取纹理槽名称
    internal static string GetTextureSlotName(IntPtr shader, int index) => initialized ? GetString(api.GetTextureSlotName, shader, index) : string.Empty;

    //读取纹理槽显示名
    internal static string GetTextureSlotDisplayName(IntPtr shader, int index) => initialized ? GetString(api.GetTextureSlotDisplayName, shader, index) : string.Empty;

    //读取纹理槽维度
    internal static int GetTextureSlotDimension(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetTextureSlotDimension(shader, index) : 0;

    //读取颜色槽名称
    internal static string GetColorSlotName(IntPtr shader, int index) => initialized ? GetString(api.GetColorSlotName, shader, index) : string.Empty;

    //读取颜色槽显示名
    internal static string GetColorSlotDisplayName(IntPtr shader, int index) => initialized ? GetString(api.GetColorSlotDisplayName, shader, index) : string.Empty;

    //读取颜色槽默认值
    internal static color4 GetColorSlotDefault(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetColorSlotDefault(shader, index) : default;

    //读取浮点槽名称
    internal static string GetFloatSlotName(IntPtr shader, int index) => initialized ? GetString(api.GetFloatSlotName, shader, index) : string.Empty;

    //读取浮点槽显示名
    internal static string GetFloatSlotDisplayName(IntPtr shader, int index) => initialized ? GetString(api.GetFloatSlotDisplayName, shader, index) : string.Empty;

    //读取浮点槽默认值
    internal static float GetFloatSlotDefault(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetFloatSlotDefault(shader, index) : 0.0f;

    //替换 Shader 源码
    internal static bool ReplaceSource(IntPtr shader, string vertexSource, string fragmentSource)
    {
        if (!initialized || shader == IntPtr.Zero || api.ReplaceSource == null) return false;

        byte[] vertexBytes = Encoding.UTF8.GetBytes(vertexSource ?? string.Empty);
        byte[] fragmentBytes = Encoding.UTF8.GetBytes(fragmentSource ?? string.Empty);
        fixed (byte* vertexPointer = vertexBytes)
        fixed (byte* fragmentPointer = fragmentBytes)
        {
            return api.ReplaceSource(shader, vertexPointer, vertexBytes.Length, fragmentPointer, fragmentBytes.Length) != 0;
        }
    }

    //调用字符串输入并返回指针
    private static IntPtr CallString(delegate* unmanaged[Cdecl]<byte*, int, IntPtr> function, string? value)
    {
        if (function == null) return IntPtr.Zero;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length);
        }
    }

    //读取对象字符串
    private static string GetString(delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> function, IntPtr pointer)
    {
        if (function == null || pointer == IntPtr.Zero) return string.Empty;

        int requiredBytes = function(pointer, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = function(pointer, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return Encoding.UTF8.GetString(output[..length]);
        }
    }

    //读取对象索引字符串
    private static string GetString(delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> function, IntPtr pointer, int index)
    {
        if (function == null || pointer == IntPtr.Zero) return string.Empty;

        int requiredBytes = function(pointer, index, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = function(pointer, index, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return Encoding.UTF8.GetString(output[..length]);
        }
    }
}
