using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct StaticMeshRendererBindApi
{
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> GetMesh;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr, byte> SetMesh;
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
    internal static IntPtr GetMesh(EnsId ens)
    {
        return initialized && api.GetMesh != null ? api.GetMesh(ens) : IntPtr.Zero;
    }

    //写入 mesh
    internal static bool SetMesh(EnsId ens, IntPtr mesh)
    {
        if (!initialized || api.SetMesh == null) return false;
        return api.SetMesh(ens, mesh) != 0;
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
