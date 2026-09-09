using System;

namespace Orbeden;

/// <summary>C# 脚本对一个原生 Script 组件的强类型包装。</summary>
public abstract unsafe partial class Script : Component
{
    /// <summary>脚本所属 EnsId。</summary>
    public EnsId EnsId => Ens.Id;

    /// <summary>创建与原生宿主绑定的脚本包装；只能由脚本运行时调用。</summary>
    protected Script(Ens ens) : base(ens, ConsumeConstructionHost(ens))
    {
    }

    /// <summary>包装已有原生派生 Script，不建立托管构造上下文。</summary>
    protected Script(Ens ens, IntPtr pointer) : base(ens, pointer) {}
}
