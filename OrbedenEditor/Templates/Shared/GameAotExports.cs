using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Orbeden;

// NativeAOT 只导出游戏主程序集中的入口；每个阶段在此进入托管域一次。
internal static class OrbedenAotExports
{
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Initialize", CallConvs = [typeof(CallConvCdecl)])]
    public static void Initialize(IntPtr nativeApi) => GameScriptRuntime.Initialize(nativeApi);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Shutdown", CallConvs = [typeof(CallConvCdecl)])]
    public static void Shutdown() => GameScriptRuntime.Shutdown();

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Update", CallConvs = [typeof(CallConvCdecl)])]
    public static void Update(float deltaTime) => GameScriptRuntime.Update(deltaTime);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_FixedUpdate", CallConvs = [typeof(CallConvCdecl)])]
    public static void FixedUpdate(float fixedDeltaTime) => GameScriptRuntime.FixedUpdate(fixedDeltaTime);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_LateUpdate", CallConvs = [typeof(CallConvCdecl)])]
    public static void LateUpdate(float deltaTime) => GameScriptRuntime.LateUpdate(deltaTime);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_EnsWorldActiveChanged", CallConvs = [typeof(CallConvCdecl)])]
    public static void EnsWorldActiveChanged(EnsId ens, byte worldActive) =>
        GameScriptRuntime.OnEnsWorldActiveChanged(ens, worldActive != 0);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_EnsDestroyed", CallConvs = [typeof(CallConvCdecl)])]
    public static void EnsDestroyed(EnsId ens) => GameScriptRuntime.OnEnsDestroyed(ens);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_DrawGui", CallConvs = [typeof(CallConvCdecl)])]
    public static void DrawGui() => GameScriptRuntime.DrawGUI();
}
