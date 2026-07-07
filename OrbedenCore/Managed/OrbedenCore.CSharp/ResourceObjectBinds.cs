using System.Runtime.InteropServices;
using System.Text;

namespace OrbedenCore.CSharp;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct MeshBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, byte> Load;
    public delegate* unmanaged[Cdecl]<byte*, int, byte> IsValid;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<byte*, int, int> GetVertexCount;
    public delegate* unmanaged[Cdecl]<byte*, int, int> GetIndexCount;
    public delegate* unmanaged[Cdecl]<byte*, int, int> GetSubMeshCount;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte*, int, int> GetSubMeshName;
    public delegate* unmanaged[Cdecl]<byte*, int, int, uint> GetSubMeshIndexStart;
    public delegate* unmanaged[Cdecl]<byte*, int, int, uint> GetSubMeshIndexCount;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte*, int, int> GetSubMeshMaterial;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct MaterialBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, byte> Load;
    public delegate* unmanaged[Cdecl]<byte*, int, byte> IsValid;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> GetShader;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> SetShader;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> HasTexture;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, int> GetTexture;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, byte> SetTexture;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> ClearTexture;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> HasColor;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, color4, color4> GetColor;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, color4, byte> SetColor;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> ClearColor;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> HasFloat;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, float, float> GetFloat;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, float, byte> SetFloat;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte> ClearFloat;
    public delegate* unmanaged[Cdecl]<byte*, int, ulong> GetRevision;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ShaderBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, byte> Load;
    public delegate* unmanaged[Cdecl]<byte*, int, byte> IsValid;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> GetVertexPath;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> GetFragmentPath;
    public delegate* unmanaged[Cdecl]<byte*, int, int> GetTextureSlotCount;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte*, int, int> GetTextureSlotName;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte*, int, int> GetTextureSlotDisplayName;
    public delegate* unmanaged[Cdecl]<byte*, int, int, int> GetTextureSlotDimension;
    public delegate* unmanaged[Cdecl]<byte*, int, int> GetColorSlotCount;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte*, int, int> GetColorSlotName;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte*, int, int> GetColorSlotDisplayName;
    public delegate* unmanaged[Cdecl]<byte*, int, int, color4> GetColorSlotDefault;
    public delegate* unmanaged[Cdecl]<byte*, int, int> GetFloatSlotCount;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte*, int, int> GetFloatSlotName;
    public delegate* unmanaged[Cdecl]<byte*, int, int, byte*, int, int> GetFloatSlotDisplayName;
    public delegate* unmanaged[Cdecl]<byte*, int, int, float> GetFloatSlotDefault;
}
#pragma warning restore CS0649

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
    internal static bool Load(string key)
    {
        return initialized && NativeText.CallBool(api.Load, key);
    }

    //判断 Mesh 是否有效
    internal static bool IsValid(string key)
    {
        return initialized && NativeText.CallBool(api.IsValid, key);
    }

    //读取 Mesh 名称
    internal static string GetName(string key)
    {
        return initialized ? NativeText.GetString(api.GetName, key) : string.Empty;
    }

    //读取顶点数量
    internal static int GetVertexCount(string key)
    {
        return initialized ? NativeText.CallInt(api.GetVertexCount, key) : 0;
    }

    //读取索引数量
    internal static int GetIndexCount(string key)
    {
        return initialized ? NativeText.CallInt(api.GetIndexCount, key) : 0;
    }

    //读取子网格数量
    internal static int GetSubMeshCount(string key)
    {
        return initialized ? NativeText.CallInt(api.GetSubMeshCount, key) : 0;
    }

    //读取子网格名称
    internal static string GetSubMeshName(string key, int index)
    {
        return initialized ? NativeText.GetString(api.GetSubMeshName, key, index) : string.Empty;
    }

    //读取子网格起始索引
    internal static uint GetSubMeshIndexStart(string key, int index)
    {
        return initialized ? NativeText.CallUInt(api.GetSubMeshIndexStart, key, index) : 0;
    }

    //读取子网格索引数量
    internal static uint GetSubMeshIndexCount(string key, int index)
    {
        return initialized ? NativeText.CallUInt(api.GetSubMeshIndexCount, key, index) : 0;
    }

    //读取子网格材质 Key
    internal static string GetSubMeshMaterial(string key, int index)
    {
        return initialized ? NativeText.GetString(api.GetSubMeshMaterial, key, index) : string.Empty;
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
    internal static bool Load(string key)
    {
        return initialized && NativeText.CallBool(api.Load, key);
    }

    //判断 Material 是否有效
    internal static bool IsValid(string key)
    {
        return initialized && NativeText.CallBool(api.IsValid, key);
    }

    //读取 Material 名称
    internal static string GetName(string key)
    {
        return initialized ? NativeText.GetString(api.GetName, key) : string.Empty;
    }

    //读取 Shader Key
    internal static string GetShader(string key)
    {
        return initialized ? NativeText.GetString(api.GetShader, key) : string.Empty;
    }

    //写入 Shader Key
    internal static bool SetShader(string key, string shaderKey)
    {
        return initialized && NativeText.CallBool(api.SetShader, key, shaderKey);
    }

    //判断纹理槽是否存在
    internal static bool HasTexture(string key, string slotName)
    {
        return initialized && NativeText.CallBool(api.HasTexture, key, slotName);
    }

    //读取纹理 Key
    internal static string GetTexture(string key, string slotName)
    {
        return initialized ? NativeText.GetString(api.GetTexture, key, slotName) : string.Empty;
    }

    //写入纹理 Key
    internal static bool SetTexture(string key, string slotName, string textureKey)
    {
        return initialized && NativeText.CallBool(api.SetTexture, key, slotName, textureKey);
    }

    //清除纹理槽
    internal static bool ClearTexture(string key, string slotName)
    {
        return initialized && NativeText.CallBool(api.ClearTexture, key, slotName);
    }

    //判断颜色槽是否存在
    internal static bool HasColor(string key, string slotName)
    {
        return initialized && NativeText.CallBool(api.HasColor, key, slotName);
    }

    //读取颜色槽
    internal static color4 GetColor(string key, string slotName, color4 defaultValue)
    {
        if (!initialized || api.GetColor == null) return defaultValue;

        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName);
        fixed (byte* keyPointer = keyBytes)
        fixed (byte* slotPointer = slotBytes)
        {
            return api.GetColor(keyPointer, keyBytes.Length, slotPointer, slotBytes.Length, defaultValue);
        }
    }

    //写入颜色槽
    internal static bool SetColor(string key, string slotName, color4 value)
    {
        if (!initialized || api.SetColor == null) return false;

        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName);
        fixed (byte* keyPointer = keyBytes)
        fixed (byte* slotPointer = slotBytes)
        {
            return api.SetColor(keyPointer, keyBytes.Length, slotPointer, slotBytes.Length, value) != 0;
        }
    }

    //清除颜色槽
    internal static bool ClearColor(string key, string slotName)
    {
        return initialized && NativeText.CallBool(api.ClearColor, key, slotName);
    }

    //判断浮点槽是否存在
    internal static bool HasFloat(string key, string slotName)
    {
        return initialized && NativeText.CallBool(api.HasFloat, key, slotName);
    }

    //读取浮点槽
    internal static float GetFloat(string key, string slotName, float defaultValue)
    {
        if (!initialized || api.GetFloat == null) return defaultValue;

        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName);
        fixed (byte* keyPointer = keyBytes)
        fixed (byte* slotPointer = slotBytes)
        {
            return api.GetFloat(keyPointer, keyBytes.Length, slotPointer, slotBytes.Length, defaultValue);
        }
    }

    //写入浮点槽
    internal static bool SetFloat(string key, string slotName, float value)
    {
        if (!initialized || api.SetFloat == null) return false;

        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName);
        fixed (byte* keyPointer = keyBytes)
        fixed (byte* slotPointer = slotBytes)
        {
            return api.SetFloat(keyPointer, keyBytes.Length, slotPointer, slotBytes.Length, value) != 0;
        }
    }

    //清除浮点槽
    internal static bool ClearFloat(string key, string slotName)
    {
        return initialized && NativeText.CallBool(api.ClearFloat, key, slotName);
    }

    //读取材质版本
    internal static ulong GetRevision(string key)
    {
        return initialized ? NativeText.CallULong(api.GetRevision, key) : 0;
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
    internal static bool Load(string key)
    {
        return initialized && NativeText.CallBool(api.Load, key);
    }

    //判断 Shader 是否有效
    internal static bool IsValid(string key)
    {
        return initialized && NativeText.CallBool(api.IsValid, key);
    }

    //读取 Shader 名称
    internal static string GetName(string key)
    {
        return initialized ? NativeText.GetString(api.GetName, key) : string.Empty;
    }

    //读取顶点源码路径
    internal static string GetVertexPath(string key)
    {
        return initialized ? NativeText.GetString(api.GetVertexPath, key) : string.Empty;
    }

    //读取片元源码路径
    internal static string GetFragmentPath(string key)
    {
        return initialized ? NativeText.GetString(api.GetFragmentPath, key) : string.Empty;
    }

    //读取纹理槽数量
    internal static int GetTextureSlotCount(string key)
    {
        return initialized ? NativeText.CallInt(api.GetTextureSlotCount, key) : 0;
    }

    //读取纹理槽名称
    internal static string GetTextureSlotName(string key, int index)
    {
        return initialized ? NativeText.GetString(api.GetTextureSlotName, key, index) : string.Empty;
    }

    //读取纹理槽显示名
    internal static string GetTextureSlotDisplayName(string key, int index)
    {
        return initialized ? NativeText.GetString(api.GetTextureSlotDisplayName, key, index) : string.Empty;
    }

    //读取纹理槽维度
    internal static int GetTextureSlotDimension(string key, int index)
    {
        if (!initialized || api.GetTextureSlotDimension == null) return 0;

        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        fixed (byte* keyPointer = keyBytes)
        {
            return api.GetTextureSlotDimension(keyPointer, keyBytes.Length, index);
        }
    }

    //读取颜色槽数量
    internal static int GetColorSlotCount(string key)
    {
        return initialized ? NativeText.CallInt(api.GetColorSlotCount, key) : 0;
    }

    //读取颜色槽名称
    internal static string GetColorSlotName(string key, int index)
    {
        return initialized ? NativeText.GetString(api.GetColorSlotName, key, index) : string.Empty;
    }

    //读取颜色槽显示名
    internal static string GetColorSlotDisplayName(string key, int index)
    {
        return initialized ? NativeText.GetString(api.GetColorSlotDisplayName, key, index) : string.Empty;
    }

    //读取颜色槽默认值
    internal static color4 GetColorSlotDefault(string key, int index)
    {
        if (!initialized || api.GetColorSlotDefault == null) return new color4(0.0f, 0.0f, 0.0f, 1.0f);

        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        fixed (byte* keyPointer = keyBytes)
        {
            return api.GetColorSlotDefault(keyPointer, keyBytes.Length, index);
        }
    }

    //读取浮点槽数量
    internal static int GetFloatSlotCount(string key)
    {
        return initialized ? NativeText.CallInt(api.GetFloatSlotCount, key) : 0;
    }

    //读取浮点槽名称
    internal static string GetFloatSlotName(string key, int index)
    {
        return initialized ? NativeText.GetString(api.GetFloatSlotName, key, index) : string.Empty;
    }

    //读取浮点槽显示名
    internal static string GetFloatSlotDisplayName(string key, int index)
    {
        return initialized ? NativeText.GetString(api.GetFloatSlotDisplayName, key, index) : string.Empty;
    }

    //读取浮点槽默认值
    internal static float GetFloatSlotDefault(string key, int index)
    {
        if (!initialized || api.GetFloatSlotDefault == null) return 0.0f;

        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        fixed (byte* keyPointer = keyBytes)
        {
            return api.GetFloatSlotDefault(keyPointer, keyBytes.Length, index);
        }
    }
}
