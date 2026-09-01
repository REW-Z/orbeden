using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

/// <summary>引擎内置游戏脚本模块 ABI。</summary>
public static unsafe class GameModule
{
    /// <summary>装载 Editor CLR 模式的用户游戏程序集。</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static byte OrbedenGame_LoadAssembly(byte* assemblyPath, int assemblyPathLength)
    {
        if (assemblyPath == null || assemblyPathLength <= 0) return 0;

        string path = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(assemblyPath, assemblyPathLength));
        return ScriptRuntime.LoadGameAssembly(path) ? (byte)1 : (byte)0;
    }

    /// <summary>初始化当前 World 的脚本运行时。</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Initialize(IntPtr nativeApi)
    {
        ScriptRuntime.Initialize(nativeApi);
    }

    /// <summary>关闭当前 World 的脚本运行时。</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Shutdown()
    {
        ScriptRuntime.Shutdown();
    }

    /// <summary>更新已挂载脚本。</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Update(float deltaTime)
    {
        ScriptRuntime.Update(deltaTime);
    }

    /// <summary>固定步长更新已挂载脚本。</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_FixedUpdate(float fixedDeltaTime)
    {
        ScriptRuntime.FixedUpdate(fixedDeltaTime);
    }

    /// <summary>执行当前 World 的托管脚本 LateUpdate 阶段。</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_LateUpdate(float deltaTime)
    {
        ScriptRuntime.LateUpdate(deltaTime);
    }

    /// <summary>接收 Ens 层级活动状态变化。</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_EnsWorldActiveChanged(EnsId ens, byte worldActive)
    {
        ScriptRuntime.OnEnsWorldActiveChanged(ens, worldActive != 0);
    }

    /// <summary>接收 Ens 销毁事件。</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_EnsDestroyed(EnsId ens)
    {
        ScriptRuntime.OnEnsDestroyed(ens);
    }

    /// <summary>绘制已挂载脚本 GUI。</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_DrawGui()
    {
        ScriptRuntime.DrawGUI();
    }
}
