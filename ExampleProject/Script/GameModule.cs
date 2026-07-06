using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OrbedenCore.CSharp;

namespace ExampleGame;

/// <summary>ExampleGame AOT 模块入口。</summary>
public static class GameModule
{
    private static SampleBehaviour? sampleBehaviour;

    /// <summary>初始化游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Initialize", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Initialize(IntPtr nativeApi)
    {
        OrbedenCoreRuntime.Initialize(nativeApi);

        Ens ens = Ens.Find("world://examples/world/cube");
        if (!ens.IsValid)
        {
            ens = Ens.Create("AOT Sample Ens");
        }

        sampleBehaviour = new SampleBehaviour(ens);
        sampleBehaviour.InvokeStart();
    }

    /// <summary>关闭游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Shutdown", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Shutdown()
    {
        sampleBehaviour?.InvokeEnd();
        sampleBehaviour = null;
    }

    /// <summary>更新游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Update", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Update(float deltaTime)
    {
        sampleBehaviour?.InvokeUpdate(deltaTime);
    }

    /// <summary>绘制游戏模块 GUI。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_DrawGui", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_DrawGui()
    {
        GuiOverlay.Draw();
    }
}
