using System;

namespace Orbeden;

/// <summary>供游戏主程序集的固定 NativeAOT 导出薄层调用。</summary>
public static class GameScriptRuntime
{
    /// <summary>初始化当前 World 的脚本运行时。</summary>
    public static void Initialize(IntPtr nativeApi) => ScriptRuntime.Initialize(nativeApi);

    /// <summary>关闭当前 World 的脚本运行时。</summary>
    public static void Shutdown() => ScriptRuntime.Shutdown();

    /// <summary>批量执行 Update delegate 表。</summary>
    public static void Update(float deltaTime) => ScriptRuntime.Update(deltaTime);

    /// <summary>批量执行 FixedUpdate delegate 表。</summary>
    public static void FixedUpdate(float fixedDeltaTime) => ScriptRuntime.FixedUpdate(fixedDeltaTime);

    /// <summary>批量执行 LateUpdate delegate 表。</summary>
    public static void LateUpdate(float deltaTime) => ScriptRuntime.LateUpdate(deltaTime);

    /// <summary>批量执行 DrawGUI delegate 表。</summary>
    public static void DrawGUI() => ScriptRuntime.DrawGUI();

    /// <summary>转发 Ens 层级活动状态变化。</summary>
    public static void OnEnsWorldActiveChanged(EnsId ens, bool worldActive) =>
        ScriptRuntime.OnEnsWorldActiveChanged(ens, worldActive);

    /// <summary>转发 Ens 销毁事件。</summary>
    public static void OnEnsDestroyed(EnsId ens) => ScriptRuntime.OnEnsDestroyed(ens);
}
