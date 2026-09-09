using System;

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>托管组件包装基类。</summary>
public abstract partial class Component : Object
{
    /// <summary>组件所属 Ens。</summary>
    public Ens Ens { get; }

    /// <summary>创建托管脚本组件包装。</summary>
    protected Component(Ens ens)
    {
        Ens = ens;
    }

    /// <summary>创建组件包装。</summary>
    protected Component(Ens ens, IntPtr pointer) : base(pointer)
    {
        Ens = ens;
    }
}

