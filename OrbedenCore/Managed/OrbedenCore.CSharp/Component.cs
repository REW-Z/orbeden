using System;

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>托管组件包装基类。</summary>
public abstract partial class Component : Object
{
    /// <summary>组件所属 Ens。</summary>
    public Ens Ens { get; }

    /// <summary>从所属节点或其父级开始查找匹配组件，包含未激活节点。</summary>
    public T? FindComponentInAncestors<T>(bool includeSelf = true) where T : Component => Ens.FindComponentInAncestors<T>(includeSelf);

    /// <summary>收集所属节点及后代节点的匹配组件，包含未激活节点。</summary>
    public T[] CollectComponentsInDescendants<T>(bool includeSelf = true) where T : Component => Ens.CollectComponentsInDescendants<T>(includeSelf);

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

