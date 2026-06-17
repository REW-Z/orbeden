namespace Orbeden;

/// <summary>托管脚本行为基类。</summary>
public abstract class ScriptBehaviour
{
    /// <summary>脚本所属 Ens。</summary>
    public Ens Ens { get; internal set; } = new Ens(EnsId.Null);

    /// <summary>脚本所属 EnsId。</summary>
    public EnsId EnsId => Ens.Id;

    //由 Runtime 调用脚本启动回调
    internal void InvokeStart()
    {
        OnStart();
    }

    //由 Runtime 调用脚本每帧回调
    internal void InvokeUpdate(float deltaTime)
    {
        OnUpdate(deltaTime);
    }

    //由 Runtime 调用脚本结束回调
    internal void InvokeEnd()
    {
        OnEnd();
    }

    /// <summary>脚本启动时调用一次。</summary>
    protected virtual void OnStart() {}

    /// <summary>脚本每帧更新时调用。</summary>
    protected virtual void OnUpdate(float deltaTime) {}

    /// <summary>脚本结束时调用一次。</summary>
    protected virtual void OnEnd() {}
}
