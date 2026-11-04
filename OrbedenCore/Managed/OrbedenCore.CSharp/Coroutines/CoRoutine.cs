using System.Collections.Generic;

namespace Orbeden;

/// <summary>协程运行句柄，也可作为其他协程的等待项</summary>
public sealed class CoRoutine : CoOperation
{
    internal object? owner;
    internal IEnumerator<CoOperation?>? routine;
    internal CoOperation? operation;
    internal ulong nextFrame;
    internal double elapsedSeconds;
    internal bool executing;

    /// <summary>是否已经结束，包括正常结束、主动停止和失败</summary>
    public bool IsFinished { get; internal set; }

    /// <summary>是否被主动停止</summary>
    public bool WasStopped { get; internal set; }

    /// <summary>失败信息，尚未失败时为空</summary>
    public string Error { get; internal set; } = string.Empty;

    //保存宿主与枚举器
    internal CoRoutine(object owner, IEnumerator<CoOperation?> routine)
    {
        this.owner = owner;
        this.routine = routine;
    }

    /// <summary>等待协程结束</summary>
    public override bool ShouldWait(double elapsedSeconds) => !IsFinished;
}
