namespace Orbeden;

/// <summary>托管组件包装基类。</summary>
public abstract class Component
{
    /// <summary>组件所属 Ens。</summary>
    public Ens Ens { get; }

    /// <summary>创建组件包装。</summary>
    protected Component(Ens ens)
    {
        Ens = ens;
    }
}

/// <summary>Ens 空间组件包装。</summary>
public sealed class SpaceComponent : Component
{
    /// <summary>创建空间组件包装。</summary>
    internal SpaceComponent(Ens ens) : base(ens) {}

    /// <summary>父级 Ens。</summary>
    public Ens parent
    {
        get => new(SpaceComponentBind.GetParent(Ens.Id));
        set => SpaceComponentBind.SetParent(Ens.Id, value.Id);
    }

    /// <summary>本地位置。</summary>
    public vector3 localPosition
    {
        get => SpaceComponentBind.GetLocalPosition(Ens.Id);
        set => SpaceComponentBind.SetLocalPosition(Ens.Id, value);
    }

    /// <summary>本地旋转。</summary>
    public quaternion localRotation
    {
        get => SpaceComponentBind.GetLocalRotation(Ens.Id);
        set => SpaceComponentBind.SetLocalRotation(Ens.Id, value);
    }

    /// <summary>本地缩放。</summary>
    public vector3 localScale
    {
        get => SpaceComponentBind.GetLocalScale(Ens.Id);
        set => SpaceComponentBind.SetLocalScale(Ens.Id, value);
    }

    /// <summary>世界位置。</summary>
    public vector3 worldPosition => SpaceComponentBind.GetWorldPosition(Ens.Id);

    /// <summary>世界旋转。</summary>
    public quaternion worldRotation => SpaceComponentBind.GetWorldRotation(Ens.Id);
}

/// <summary>静态网格渲染组件包装。</summary>
public sealed class StaticMeshRenderer : Component
{
    /// <summary>创建静态网格渲染组件包装。</summary>
    internal StaticMeshRenderer(Ens ens) : base(ens) {}

    /// <summary>是否启用渲染。</summary>
    public bool enabled
    {
        get => StaticMeshRendererBind.GetEnabled(Ens.Id);
        set => StaticMeshRendererBind.SetEnabled(Ens.Id, value);
    }

    /// <summary>是否投射阴影。</summary>
    public bool castShadows
    {
        get => StaticMeshRendererBind.GetCastShadows(Ens.Id);
        set => StaticMeshRendererBind.SetCastShadows(Ens.Id, value);
    }

    /// <summary>是否接收阴影。</summary>
    public bool receiveShadows
    {
        get => StaticMeshRendererBind.GetReceiveShadows(Ens.Id);
        set => StaticMeshRendererBind.SetReceiveShadows(Ens.Id, value);
    }
}
