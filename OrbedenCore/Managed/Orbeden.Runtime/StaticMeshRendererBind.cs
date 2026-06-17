using System;

namespace Orbeden;

internal static unsafe class StaticMeshRendererBind
{
    private static delegate* unmanaged<EnsId, byte> FuncGetEnabled;
    private static delegate* unmanaged<EnsId, byte, void> FuncSetEnabled;
    private static delegate* unmanaged<EnsId, byte> FuncGetCastShadows;
    private static delegate* unmanaged<EnsId, byte, void> FuncSetCastShadows;
    private static delegate* unmanaged<EnsId, byte> FuncGetReceiveShadows;
    private static delegate* unmanaged<EnsId, byte, void> FuncSetReceiveShadows;
    private static bool initialized;

    //保存 C++ 传入的 StaticMeshRenderer 函数表
    internal static void Initialize(IntPtr bindPointer)
    {
        if (bindPointer == IntPtr.Zero)
        {
            initialized = false;
            FuncGetEnabled = null;
            FuncSetEnabled = null;
            FuncGetCastShadows = null;
            FuncSetCastShadows = null;
            FuncGetReceiveShadows = null;
            FuncSetReceiveShadows = null;
            return;
        }

        void** functions = (void**)bindPointer;
        FuncGetEnabled = (delegate* unmanaged<EnsId, byte>)functions[0];
        FuncSetEnabled = (delegate* unmanaged<EnsId, byte, void>)functions[1];
        FuncGetCastShadows = (delegate* unmanaged<EnsId, byte>)functions[2];
        FuncSetCastShadows = (delegate* unmanaged<EnsId, byte, void>)functions[3];
        FuncGetReceiveShadows = (delegate* unmanaged<EnsId, byte>)functions[4];
        FuncSetReceiveShadows = (delegate* unmanaged<EnsId, byte, void>)functions[5];
        initialized = true;
    }

    //读取 enabled
    internal static bool GetEnabled(EnsId ens)
    {
        return initialized && FuncGetEnabled != null && FuncGetEnabled(ens) != 0;
    }

    //写入 enabled
    internal static void SetEnabled(EnsId ens, bool value)
    {
        if (initialized && FuncSetEnabled != null) FuncSetEnabled(ens, value ? (byte)1 : (byte)0);
    }

    //读取 castShadows
    internal static bool GetCastShadows(EnsId ens)
    {
        return initialized && FuncGetCastShadows != null && FuncGetCastShadows(ens) != 0;
    }

    //写入 castShadows
    internal static void SetCastShadows(EnsId ens, bool value)
    {
        if (initialized && FuncSetCastShadows != null) FuncSetCastShadows(ens, value ? (byte)1 : (byte)0);
    }

    //读取 receiveShadows
    internal static bool GetReceiveShadows(EnsId ens)
    {
        return initialized && FuncGetReceiveShadows != null && FuncGetReceiveShadows(ens) != 0;
    }

    //写入 receiveShadows
    internal static void SetReceiveShadows(EnsId ens, bool value)
    {
        if (initialized && FuncSetReceiveShadows != null) FuncSetReceiveShadows(ens, value ? (byte)1 : (byte)0);
    }
}
