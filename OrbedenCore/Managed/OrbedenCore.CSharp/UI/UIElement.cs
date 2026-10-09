using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 所有 UI 组件的基类。UI 节点是带 UILayout 的 Ens，一个节点可以同时挂多个 UIElement。
/// 生命周期由 ScriptRuntime 经 IManagedComponentLifecycle 直接驱动，不扫描方法名。
/// 接口实现只负责维护节点索引，扩展方法在索引就绪之后调用，派生类型不必调用 base。
/// </summary>
[DependsOnComponent(typeof(UILayout))]
public abstract class UIElement : Script, IManagedComponentLifecycle
{
    /// <summary>创建 UI 组件包装。</summary>
    protected UIElement(Ens ens) : base(ens)
    {
    }

    //运行时可见性覆盖的来源与当前值。
    private object? visibilityOwner;
    private bool runtimeHidden;

    /// <summary>节点是否处于活动状态：启用、世界活动、画布与层级有效。</summary>
    public bool IsUIActive()
    {
        if (!GetEnabled()) return false;
        Ens owner = Ens;
        if (!owner.IsValid || !owner.WorldActive) return false;
        //画布是这条判定的终点：它自己的 GetCanvas 返回自身，再问一次就会自我递归。
        if (this is Canvas) return true;
        Canvas? canvas = GetCanvas();
        return canvas != null && canvas.IsUIActive();
    }

    /// <summary>
    /// 运行时可见性覆盖：只影响绘制与命中，不改组件的启用状态。
    /// 覆盖属于一个来源对象，来源销毁时一并撤销，配置里的启用状态始终不变。
    /// </summary>
    public bool IsRuntimeHidden => runtimeHidden;

    /// <summary>设置运行时可见性；同一个来源重复设置只保留最后一次。</summary>
    public void SetRuntimeVisible(bool visible, object owner)
    {
        ArgumentNullException.ThrowIfNull(owner);
        runtimeHidden = !visible;
        visibilityOwner = owner;
        SetGeometryDirty();
    }

    /// <summary>撤销某个来源设下的可见性覆盖；不是它设的就不动。</summary>
    public void ClearRuntimeVisible(object owner)
    {
        if (!ReferenceEquals(visibilityOwner, owner)) return;
        visibilityOwner = null;
        runtimeHidden = false;
        SetGeometryDirty();
    }

    /// <summary>取本节点上的布局组件；没有 UILayout 的节点不参与 UI 树。</summary>
    public UILayout? GetLayout() => Ens.GetComponent<UILayout>();

    /// <summary>沿祖先查找所属画布；本节点自身是画布时返回自己。</summary>
    public Canvas? GetCanvas()
    {
        if (this is Canvas self) return self;
        for (UINode? current = UIWorldContext.Current?.FindNode(EnsId); current != null; current = current.Parent)
        {
            if (current.Canvas is Canvas canvas) return canvas;
        }
        return null;
    }

    /// <summary>取父 UI 节点；父 Ens 没有 UIElement 时返回空。</summary>
    public UIElement? GetParentElement() => UIWorldContext.GetParentElement(this);

    /// <summary>按类型取直接子 UI 节点。</summary>
    public UIElement? GetChildElement<T>() where T : UIElement
    {
        foreach (UIElement child in GetChildElements())
        {
            if (child is T match) return match;
        }
        return null;
    }

    /// <summary>取全部直接子 UI 节点。</summary>
    public IReadOnlyList<UIElement> GetChildElements() => UIWorldContext.GetChildElements(this);

    /// <summary>标记本节点的几何需要重建。</summary>
    public void SetGeometryDirty() => UIWorldContext.MarkGeometryDirty(this);

    //父级变化由节点索引转发进来；派生类型的重新登记走扩展点。
    internal void NotifyReparented() => RunExtension("OnUIReparented", OnUIReparented);

    /// <summary>标记布局需要重算。</summary>
    public void SetLayoutDirty() => UIWorldContext.MarkLayoutDirty(GetLayout());

    /// <summary>节点附着到世界后的扩展点。</summary>
    protected virtual void OnUIAttached()
    {
    }

    /// <summary>节点从非活动变为活动时的扩展点。</summary>
    protected virtual void OnUIEnabled()
    {
    }

    /// <summary>节点从活动变为非活动时的扩展点；派生类型要清捕获、焦点与派生位置。</summary>
    protected virtual void OnUIDisabled()
    {
    }

    /// <summary>配置校验扩展点；非法配置由派生类型记录错误并标记不可运行。</summary>
    protected virtual void OnUIValidate()
    {
    }

    /// <summary>节点换了父级之后的扩展点；分组一类的登记在这里重新进行。</summary>
    protected virtual void OnUIReparented()
    {
    }

    /// <summary>节点分离前的扩展点；此时索引已经撤销。</summary>
    protected virtual void OnUIDetached()
    {
    }

    /// <summary>序列化前的扩展点；运行时权威值在这里同步进宿主字段快照。</summary>
    protected virtual void OnUIBeforeSerialize()
    {
    }

    void IManagedComponentLifecycle.OnComponentBeforeSerialize() => RunExtension("OnUIBeforeSerialize", OnUIBeforeSerialize);

    //索引先登记，扩展回调后执行；扩展抛出异常只影响本节点后续的干净卸载。
    void IManagedComponentLifecycle.OnComponentAttached()
    {
        UIWorldContext.RegisterElement(this);
        RunExtension("OnUIValidate", OnUIValidate);
        RunExtension("OnUIAttached", OnUIAttached);
    }

    void IManagedComponentLifecycle.OnComponentActiveChanged(bool active)
    {
        if (active)
        {
            RunExtension("OnUIEnabled", OnUIEnabled);
            return;
        }

        //禁用先交还派生位置，再让扩展清捕获、焦点与临时可见性。
        UIWorldContext.ClearDerivedPosition(EnsId);
        RunExtension("OnUIDisabled", OnUIDisabled);
    }

    void IManagedComponentLifecycle.OnComponentFieldsChanged()
    {
        RunExtension("OnUIValidate", OnUIValidate);
        if (this is UIVisual visual)
        {
            visual.SetVerticesDirty();
            visual.SetMaterialDirty();
        }
        UIWorldContext.MarkLayoutDirty(GetLayout());
    }

    void IManagedComponentLifecycle.OnComponentDetached()
    {
        //先撤销索引与缓存引用，再交给扩展做自己的清理。
        UIWorldContext.UnregisterElement(this);
        RunExtension("OnUIDetached", OnUIDetached);
    }

    //扩展回调的异常按组件路径记录，不向调用方扩散。
    private void RunExtension(string method, Action callback)
    {
        try
        {
            callback();
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"UIElement {GetType().Name}({EnsId.id}:{EnsId.version}) failed in {method}: {exception}");
        }
    }
}
