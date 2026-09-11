using System;
using System.Runtime.InteropServices;
using System.Text;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorAssetNativeApi
{
    public IntPtr Context;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> CanModifyAssets;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, byte, int> RemapLiveReferences;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> OpenWorld;
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

    /// <summary>打开项目内的另一个场景，路径以项目根为基准。</summary>
    internal static bool OpenWorld(string relativeKey)
    {
        if (api.OpenWorld == null || string.IsNullOrEmpty(relativeKey)) return false;

        byte[] keyBytes = Encoding.UTF8.GetBytes(relativeKey);
        fixed (byte* keyPointer = keyBytes)
        {
            return api.OpenWorld(api.Context, keyPointer, keyBytes.Length) != 0;
        }
    }
}
