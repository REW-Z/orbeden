using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace OrbedenCore.CSharp;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct StaticMeshRendererBindApi
{
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, byte*, int, int> GetMesh;
    public delegate* unmanaged[Cdecl]<EnsId, byte*, int, byte> SetMesh;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetCastShadows;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetCastShadows;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetReceiveShadows;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetReceiveShadows;
}
#pragma warning restore CS0649

internal static unsafe class StaticMeshRendererBind
{
    private static StaticMeshRendererBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 StaticMeshRenderer 函数表
    internal static void Initialize(StaticMeshRendererBindApi value)
    {
        api = value;
        initialized = api.GetEnabled != null;
    }

    //读取 enabled
    internal static bool GetEnabled(EnsId ens)
    {
        return initialized && api.GetEnabled != null && api.GetEnabled(ens) != 0;
    }

    //写入 enabled
    internal static void SetEnabled(EnsId ens, bool value)
    {
        if (initialized && api.SetEnabled != null) api.SetEnabled(ens, value ? (byte)1 : (byte)0);
    }

    //读取 mesh
    internal static string GetMesh(EnsId ens)
    {
        if (!initialized || api.GetMesh == null) return string.Empty;

        int requiredBytes = api.GetMesh(ens, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        byte[] output = new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = api.GetMesh(ens, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return System.Text.Encoding.UTF8.GetString(output, 0, length);
        }
    }

    //写入 mesh
    internal static bool SetMesh(EnsId ens, string key)
    {
        if (!initialized || api.SetMesh == null) return false;

        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(key ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return api.SetMesh(ens, pointer, bytes.Length) != 0;
        }
    }

    //读取 castShadows
    internal static bool GetCastShadows(EnsId ens)
    {
        return initialized && api.GetCastShadows != null && api.GetCastShadows(ens) != 0;
    }

    //写入 castShadows
    internal static void SetCastShadows(EnsId ens, bool value)
    {
        if (initialized && api.SetCastShadows != null) api.SetCastShadows(ens, value ? (byte)1 : (byte)0);
    }

    //读取 receiveShadows
    internal static bool GetReceiveShadows(EnsId ens)
    {
        return initialized && api.GetReceiveShadows != null && api.GetReceiveShadows(ens) != 0;
    }

    //写入 receiveShadows
    internal static void SetReceiveShadows(EnsId ens, bool value)
    {
        if (initialized && api.SetReceiveShadows != null) api.SetReceiveShadows(ens, value ? (byte)1 : (byte)0);
    }
}
