using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;

namespace Orbeden;

/// <summary>C# 脚本对一个原生 Script 组件的强类型包装。</summary>
[DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicMethods | DynamicallyAccessedMemberTypes.PublicFields | DynamicallyAccessedMemberTypes.NonPublicFields)]
public abstract unsafe partial class Script : Component
{
    /// <summary>启动协程并立即执行到第一个等待点</summary>
    public CoRoutine RunCo(IEnumerator<CoOperation?> routine) => ScriptRuntime.RunCo(this, routine);

    /// <summary>停止当前脚本的一个协程</summary>
    public void StopCo(CoRoutine routine) => ScriptRuntime.StopCo(this, routine);

    /// <summary>停止当前脚本的全部协程</summary>
    public void StopAllCo() => ScriptRuntime.StopAllCo(this);

    /// <summary>脚本所属 EnsId。</summary>
    public EnsId EnsId => Ens.Id;

    /// <summary>创建与原生宿主绑定的脚本包装；只能由脚本运行时调用。</summary>
    protected Script(Ens ens) : base(ens, ConsumeConstructionHost(ens))
    {
    }

    /// <summary>包装已有原生派生 Script，不建立托管构造上下文。</summary>
    protected Script(Ens ens, IntPtr pointer) : base(ens, pointer) {}
}
