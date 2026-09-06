using System;

namespace Orbeden;

/// <summary>C# 脚本对一个原生 ScriptBehaviour 组件的强类型包装。</summary>
public abstract unsafe partial class ScriptBehaviour : Component
{
    /// <summary>脚本所属 EnsId。</summary>
    public EnsId EnsId => Ens.Id;

    /// <summary>创建与原生宿主绑定的脚本包装；只能由脚本运行时调用。</summary>
    protected ScriptBehaviour(Ens ens) : base(ens, ConsumeConstructionHost(ens))
    {
    }

    /// <summary>脚本是否参与生命周期阶段；值唯一存储在原生宿主。</summary>
    public bool enabled
    {
        get => GetHostEnabled(NativePtr);
        set => SetHostEnabled(NativePtr, value);
    }

    //结束运行时注册，供 ScriptRuntime 在 End 后调用。
    internal void DetachRuntime()
    {
        ScriptRuntimeRegistry.Unregister(this);
        DisconnectNative();
    }
}
