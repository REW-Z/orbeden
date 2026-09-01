namespace Orbeden;

/// <summary>托管脚本行为基类。</summary>
public abstract class ScriptBehaviour : Component
{
    private bool isEnabled = true;

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
    internal void SetMountId(string? mountId)
    {
        MountId = mountId ?? string.Empty;
    }

    /// <summary>脚本是否参与生命周期阶段。</summary>
    public bool enabled
    {
        get => isEnabled;
        set
        {
            if (isEnabled == value) return;
            isEnabled = value;
            ScriptRuntime.OnEnabledChanged(this);
        }
    }

    //结束运行时注册，供 ScriptRuntime 在 End 后调用
    internal void DetachRuntime()
    {
        ScriptRuntimeRegistry.Unregister(this);
    }

    /// <summary>脚本启动时调用一次。</summary>
    protected virtual void OnStart() {}

    /// <summary>脚本每帧更新时调用。</summary>
    protected virtual void OnUpdate(float deltaTime) {}

    /// <summary>脚本固定步长更新时调用。</summary>
    protected virtual void OnFixedUpdate(float fixedDeltaTime) {}

    /// <summary>所有普通 Update 完成后、渲染前调用。</summary>
    protected virtual void OnLateUpdate(float deltaTime) {}

    /// <summary>脚本每个 GUI 渲染帧调用，只能在此回调的同步调用链中使用 GUI API。</summary>
    protected virtual void OnDrawGUI() {}

    /// <summary>脚本结束时调用一次。</summary>
    protected virtual void OnEnd() {}
}
