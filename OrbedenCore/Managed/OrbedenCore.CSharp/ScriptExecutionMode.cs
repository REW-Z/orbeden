namespace Orbeden;

/// <summary>脚本域的运行阶段模式；取值与原生 ScriptExecutionMode 一致。</summary>
public enum ScriptExecutionMode : uint
{
    /// <summary>编辑模式：只构造包装与帧系统，不执行游戏生命周期与交互路由。</summary>
    Editor = 0,

    /// <summary>运行模式：按完整阶段表调度脚本。</summary>
    Play = 1,
}
