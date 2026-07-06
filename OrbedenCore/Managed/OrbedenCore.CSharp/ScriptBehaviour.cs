namespace OrbedenCore.CSharp;

/// <summary>托管脚本行为基类。</summary>
public abstract class ScriptBehaviour : Component
{
    /// <summary>脚本所属 EnsId。</summary>
    public EnsId EnsId => Ens.Id;

    /// <summary>创建托管脚本行为。</summary>
    protected ScriptBehaviour(Ens ens) : base(ens)
    {
        ScriptRuntimeRegistry.Register(this);
    }

    //由 Runtime 调用脚本启动回调
    public void InvokeStart()
    {
        OnStart();
    }

    //由 Runtime 调用脚本每帧回调
    public void InvokeUpdate(float deltaTime)
    {
        OnUpdate(deltaTime);
    }

    //由 Runtime 调用脚本结束回调
    public void InvokeEnd()
    {
        try
        {
            OnEnd();
        }
        finally
        {
            ScriptRuntimeRegistry.Unregister(this);
        }
    }

    /// <summary>脚本启动时调用一次。</summary>
    protected virtual void OnStart() {}

    /// <summary>脚本每帧更新时调用。</summary>
    protected virtual void OnUpdate(float deltaTime) {}

    /// <summary>脚本结束时调用一次。</summary>
    protected virtual void OnEnd() {}
}
