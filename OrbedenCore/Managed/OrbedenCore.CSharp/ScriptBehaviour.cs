namespace Orbeden;

/// <summary>托管脚本行为基类。</summary>
public abstract class ScriptBehaviour : Component
{
    /// <summary>脚本所属 EnsId。</summary>
    public EnsId EnsId => Ens.Id;

    /// <summary>脚本挂载实例的持久化标识。</summary>
    public string MountId { get; private set; } = string.Empty;

    /// <summary>创建托管脚本行为。</summary>
    protected ScriptBehaviour(Ens ens) : base(ens)
    {
        ScriptRuntimeRegistry.Register(this);
    }

    /// <summary>由脚本装载器设置挂载实例标识。</summary>
    public void SetMountId(string? mountId)
    {
        MountId = mountId ?? string.Empty;
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

    //由 Runtime 调用脚本固定步长回调
    public void InvokeFixedUpdate(float fixedDeltaTime)
    {
        OnFixedUpdate(fixedDeltaTime);
    }

    //由 Runtime 调用脚本 GUI 绘制回调
    public void InvokeDrawGUI()
    {
        OnDrawGUI();
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

    /// <summary>脚本固定步长更新时调用。</summary>
    protected virtual void OnFixedUpdate(float fixedDeltaTime) {}

    /// <summary>脚本每个 GUI 渲染帧调用，只能在此回调的同步调用链中使用 GUI API。</summary>
    protected virtual void OnDrawGUI() {}

    /// <summary>脚本结束时调用一次。</summary>
    protected virtual void OnEnd() {}
}
