using System;

namespace Orbeden;

/// <summary>托管组件包装基类。</summary>
public abstract class Component : Object
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

/// <summary>Ens 空间组件包装。</summary>
public sealed class SpaceComponent : Component
{
    /// <summary>创建空间组件包装。</summary>
    internal SpaceComponent(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    //从原生指针获取 SpaceComponent 包装
    internal static SpaceComponent? FromNative(Ens ens, IntPtr pointer)
    {
        return Object.FromNative(pointer, value => new SpaceComponent(ens, value));
    }

    /// <summary>父级 Ens。</summary>
    public Ens parent
    {
        get => Ens.FromId(SpaceComponentBind.GetParent(Ens.Id));
        set => SpaceComponentBind.SetParent(Ens.Id, value != null ? value.Id : EnsId.Null);
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
    internal StaticMeshRenderer(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    //从原生指针获取 StaticMeshRenderer 包装
    internal static StaticMeshRenderer? FromNative(Ens ens, IntPtr pointer)
    {
        return Object.FromNative(pointer, value => new StaticMeshRenderer(ens, value));
    }

    /// <summary>是否启用渲染。</summary>
    public bool enabled
    {
        get => StaticMeshRendererBind.GetEnabled(Ens.Id);
        set => StaticMeshRendererBind.SetEnabled(Ens.Id, value);
    }

    /// <summary>渲染使用的 Mesh 资源。</summary>
    public Mesh? mesh
    {
        get => Mesh.FromNative(StaticMeshRendererBind.GetMesh(Ens.Id));
        set => StaticMeshRendererBind.SetMesh(Ens.Id, value?.NativePtr ?? IntPtr.Zero);
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
