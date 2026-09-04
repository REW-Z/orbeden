using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

internal static unsafe partial class ManagedScriptInterop
{
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus HostAttachedAbi(IntPtr host)
    {
        try { return ScriptRuntime.OnHostAttached(host); }
        catch { return InteropStatus.InvocationFailed; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus HostDetachedAbi(IntPtr host)
    {
        try { return ScriptRuntime.OnHostDetached(host); }
        catch { return InteropStatus.InvocationFailed; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus HostEnabledChangedAbi(IntPtr host)
    {
        try { return ScriptRuntime.OnHostEnabledChanged(host); }
        catch { return InteropStatus.InvocationFailed; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus HostFieldChangedAbi(IntPtr host, byte* name, int length)
    {
        try
        {
            if (host == IntPtr.Zero || name == null || length <= 0)
                return InteropStatus.InvalidArgument;
            return ScriptRuntime.OnHostFieldChanged(host, Encoding.UTF8.GetString(name, length));
        }
        catch { return InteropStatus.InvocationFailed; }
    }
}
