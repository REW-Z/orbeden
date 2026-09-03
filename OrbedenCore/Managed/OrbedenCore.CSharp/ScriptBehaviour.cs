namespace Orbeden;

/// <summary>托管脚本行为基类；生命周期由派生脚本按约定名称声明为非虚实例方法。</summary>
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
}
