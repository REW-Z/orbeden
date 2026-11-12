using System;
using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ProfilerBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, ProfileScope*, void> BeginSample;
    public delegate* unmanaged[Cdecl]<ProfileScope*, void> EndSample;
}
#pragma warning restore CS0649

/// <summary>在 Core Profiler 中记录当前线程的同步代码段</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct ProfileScope : IDisposable
{
    private static ProfilerBindApi api;
    internal IntPtr Sample;
    internal long StartedAtNanoseconds;

    //保存 Core 性能采样函数表
    internal static void InitializeNativeApi(ProfilerBindApi value) => api = value;

    /// <summary>使用稳定的 UTF-8 名称开始采样，未采集时跳过计时</summary>
    public ProfileScope(ReadOnlySpan<byte> name)
    {
        this = default;
        if (api.BeginSample == null || name.IsEmpty)
        {
            return;
        }
        fixed (byte* text = name)
        fixed (ProfileScope* scope = &this)
        {
            api.BeginSample(text, name.Length, scope);
        }
    }

    /// <summary>结束采样，作用域按同线程的逆序释放</summary>
    public void Dispose()
    {
        if (Sample == IntPtr.Zero || api.EndSample == null)
        {
            return;
        }
        fixed (ProfileScope* scope = &this)
        {
            api.EndSample(scope);
        }
    }
}
