using System;

namespace Orbeden;

/// <summary>协程等待项，可由业务派生实现自定义等待</summary>
public abstract class CoOperation
{
    /// <summary>根据本次等待经过的有效更新秒数，返回是否继续等待</summary>
    public abstract bool ShouldWait(double elapsedSeconds);
}

/// <summary>等待指定的有效更新秒数，宿主停用或游戏暂停时停止计时</summary>
public sealed class Wait : CoOperation
{
    private readonly double seconds;

    /// <summary>创建指定秒数的等待项</summary>
    public Wait(double seconds)
    {
        if (!double.IsFinite(seconds) || seconds < 0.0)
        {
            throw new ArgumentOutOfRangeException(nameof(seconds));
        }

        this.seconds = seconds;
    }

    /// <summary>检查等待时间</summary>
    public override bool ShouldWait(double elapsedSeconds) => elapsedSeconds < seconds;
}

/// <summary>等待条件成立</summary>
public sealed class WaitUntil : CoOperation
{
    private readonly Func<bool> predicate;

    /// <summary>创建条件成立时恢复的等待项</summary>
    public WaitUntil(Func<bool> predicate)
    {
        ArgumentNullException.ThrowIfNull(predicate);
        this.predicate = predicate;
    }

    /// <summary>检查恢复条件</summary>
    public override bool ShouldWait(double elapsedSeconds) => !predicate();
}

/// <summary>等待条件不再成立</summary>
public sealed class WaitWhile : CoOperation
{
    private readonly Func<bool> predicate;

    /// <summary>创建条件不再成立时恢复的等待项</summary>
    public WaitWhile(Func<bool> predicate)
    {
        ArgumentNullException.ThrowIfNull(predicate);
        this.predicate = predicate;
    }

    /// <summary>检查等待条件</summary>
    public override bool ShouldWait(double elapsedSeconds) => predicate();
}
