using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Orbeden;

namespace ExampleGame;

/// <summary>ExampleGame AOT 模块入口。</summary>
public static class GameModule
{
    /// <summary>初始化游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Initialize", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Initialize(IntPtr nativeApi)
    {
        OrbedenCoreRuntime.Initialize(nativeApi);
        MountedScriptRuntime.Initialize();
    }

    /// <summary>关闭游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Shutdown", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Shutdown()
    {
        MountedScriptRuntime.Shutdown();
    }

    /// <summary>更新游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Update", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Update(float deltaTime)
    {
        MountedScriptRuntime.Update(deltaTime);
    }

    /// <summary>绘制游戏模块 GUI。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_DrawGui", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_DrawGui()
    {
        GuiOverlay.Draw();
    }
}
