using System;
using System.Diagnostics.CodeAnalysis;

namespace Orbeden;

/// <summary>脚本类型里已经不存在、对账时从宿主删掉的字段。撤销要把它们原样写回去。</summary>
public readonly record struct DroppedScriptField(string Name, string TypeName, string Value);

/// <summary>供游戏主程序集的固定 NativeAOT 导出薄层调用。</summary>
public static class GameScriptRuntime
{
    /// <summary>供 Editor 为新宿主补齐构造函数和字段初始化器的默认值。
    /// 返回按脚本类型对账时删掉的字段，供编辑器撤销。</summary>
    public static IReadOnlyList<DroppedScriptField> InitializeEditorHost(IntPtr binding, IntPtr host, Ens ens,
        [DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicConstructors)] Type type) =>
        Script.InitializeEditorHost(binding, host, ens, type);

    /// <summary>把字段写回宿主字段表；撤销删除时用，字段可见性按可见写回。</summary>
    public static bool WriteHostField(IntPtr host, string name, string typeName, string value) =>
        Script.WriteHostField(host, name, typeName, value, true);

    /// <summary>从宿主字段表删除字段；重做删除时用。</summary>
    public static bool RemoveHostField(IntPtr host, string name) => Script.RemoveHostField(host, name);

    /// <summary>初始化当前 World 的脚本运行时；executionMode 为 ScriptExecutionMode。</summary>
    public static void Initialize(IntPtr nativeApi, uint executionMode) => ScriptRuntime.Initialize(nativeApi, executionMode);

    /// <summary>处理托管输入阶段。</summary>
    public static void ProcessInput(float deltaTime) => ScriptRuntime.ProcessInput(deltaTime);

    /// <summary>准备托管渲染阶段。</summary>
    public static void PrepareRender(float deltaTime) => ScriptRuntime.PrepareRender(deltaTime);

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
