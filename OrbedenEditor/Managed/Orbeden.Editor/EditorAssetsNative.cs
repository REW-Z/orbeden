using System;
using System.Runtime.InteropServices;
using System.Text;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorAssetNativeApi
{
    public IntPtr Context;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetResourceRoot;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> CanModifyAssets;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, byte, int> RemapLiveReferences;
}
#pragma warning restore CS0649

/// <summary>Editor 资源操作使用的原生桥。</summary>
internal static unsafe class EditorAssetsNative
{
    private static EditorAssetNativeApi api;

    /// <summary>保存原生资源 API。</summary>
    internal static void Initialize(EditorAssetNativeApi value)
    {
        api = value;
    }

    /// <summary>读取项目配置中的资源根目录。</summary>
    internal static string GetResourceRoot()
    {
        if (api.GetResourceRoot == null) return "Resource";

        int requiredBytes = api.GetResourceRoot(api.Context, null, 0);
        if (requiredBytes <= 0) return "Resource";

        Span<byte> bytes = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* output = bytes)
        {
            int actualBytes = api.GetResourceRoot(api.Context, output, requiredBytes);
            return Encoding.UTF8.GetString(bytes[..Math.Clamp(actualBytes, 0, requiredBytes)]);
        }
    }

    /// <summary>判断当前是否允许修改资源文件。</summary>
    internal static bool CanModifyAssets()
    {
        return api.CanModifyAssets != null && api.CanModifyAssets(api.Context) != 0;
    }

    /// <summary>批量重映射当前进程内存活的原生 Ref 字段。</summary>
    internal static int RemapLiveReferences(string oldKey, string newKey, bool prefix)
    {
        if (api.RemapLiveReferences == null || string.IsNullOrEmpty(oldKey)) return 0;

        byte[] oldBytes = Encoding.UTF8.GetBytes(oldKey);
        byte[] newBytes = Encoding.UTF8.GetBytes(newKey ?? string.Empty);
        fixed (byte* oldPointer = oldBytes)
        fixed (byte* newPointer = newBytes)
        {
            return api.RemapLiveReferences(api.Context,
                oldPointer,
                oldBytes.Length,
                newPointer,
                newBytes.Length,
                prefix ? (byte)1 : (byte)0);
        }
    }
}
