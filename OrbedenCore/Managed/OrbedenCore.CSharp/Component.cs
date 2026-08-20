using System;

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

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

/// <summary>Ens 变换组件包装。</summary>
[UniqueComponent]
public sealed partial class TransformComponent : Component
{
    /// <summary>创建变换组件包装。</summary>
    internal TransformComponent(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    //从原生指针获取 TransformComponent 包装
    internal static TransformComponent? FromNative(Ens ens, IntPtr pointer)
    {
        return Object.FromNative(pointer, value => new TransformComponent(ens, value));
    }

    /// <summary>父级 Ens。</summary>
    public Ens parent
    {
        get => Ens.FromId(GetParent(Ens.Id));
        set => SetParent(Ens.Id, value != null ? value.Id : EnsId.Null);
    }

    /// <summary>本地位置。</summary>
    public vector3 localPosition
    {
        get => GetLocalPosition(Ens.Id);
        set => SetLocalPosition(Ens.Id, value);
    }

    /// <summary>本地旋转。</summary>
    public quaternion localRotation
    {
        get => GetLocalRotation(Ens.Id);
        set => SetLocalRotation(Ens.Id, value);
    }

    /// <summary>本地缩放。</summary>
    public vector3 localScale
    {
        get => GetLocalScale(Ens.Id);
        set => SetLocalScale(Ens.Id, value);
    }

    /// <summary>世界位置。</summary>
    public vector3 worldPosition => GetWorldPosition(Ens.Id);

    /// <summary>世界旋转。</summary>
    public quaternion worldRotation => GetWorldRotation(Ens.Id);
}

/// <summary>静态网格绘制队列。</summary>
public enum DrawQueue : uint
{
    Opaque = 0,
    Transparent = 1,
    Refraction = 2,
}

/// <summary>静态网格渲染组件包装。</summary>
[UniqueComponent]
public sealed partial class StaticMeshRenderer : Component
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
        get => GetEnabled(Ens.Id);
        set => SetEnabled(Ens.Id, value);
    }

    /// <summary>渲染使用的 Mesh 资源。</summary>
    public Mesh? mesh
    {
        get => Mesh.FromNative(GetMesh(Ens.Id));
        set => SetMesh(Ens.Id, value?.NativePtr ?? IntPtr.Zero);
    }

    /// <summary>渲染器所在的原生绘制阶段。</summary>
    public DrawQueue drawQueue
    {
        get => GetDrawQueue(Ens.Id);
        set => SetDrawQueue(Ens.Id, value);
    }

    /// <summary>是否投射阴影。</summary>
    public bool castShadows
    {
        get => GetCastShadows(Ens.Id);
        set => SetCastShadows(Ens.Id, value);
    }

    /// <summary>是否接收阴影。</summary>
    public bool receiveShadows
    {
        get => GetReceiveShadows(Ens.Id);
        set => SetReceiveShadows(Ens.Id, value);
    }
}

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct TransformComponentBindApi
{
    public delegate* unmanaged[Cdecl]<EnsId, EnsId> GetParent;
    public delegate* unmanaged[Cdecl]<EnsId, EnsId, void> SetParent;
    public delegate* unmanaged[Cdecl]<EnsId, vector3> GetLocalPosition;
    public delegate* unmanaged[Cdecl]<EnsId, vector3, void> SetLocalPosition;
    public delegate* unmanaged[Cdecl]<EnsId, quaternion> GetLocalRotation;
    public delegate* unmanaged[Cdecl]<EnsId, quaternion, void> SetLocalRotation;
    public delegate* unmanaged[Cdecl]<EnsId, vector3> GetLocalScale;
    public delegate* unmanaged[Cdecl]<EnsId, vector3, void> SetLocalScale;
    public delegate* unmanaged[Cdecl]<EnsId, vector3> GetWorldPosition;
    public delegate* unmanaged[Cdecl]<EnsId, quaternion> GetWorldRotation;
}
#pragma warning restore CS0649

public sealed unsafe partial class TransformComponent
{
private static TransformComponentBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 TransformComponent 函数表
    internal static void InitializeNativeApi(TransformComponentBindApi value)
    {
        api = value;
        initialized = api.GetParent != null;
    }

    //读取 parent
    internal static EnsId GetParent(EnsId ens)
    {
        return initialized && api.GetParent != null ? api.GetParent(ens) : EnsId.Null;
    }

    //写入 parent
    internal static void SetParent(EnsId ens, EnsId parent)
    {
        if (initialized && api.SetParent != null) api.SetParent(ens, parent);
    }

    //读取 localPosition
    internal static vector3 GetLocalPosition(EnsId ens)
    {
        return initialized && api.GetLocalPosition != null ? api.GetLocalPosition(ens) : default;
    }

    //写入 localPosition
    internal static void SetLocalPosition(EnsId ens, vector3 value)
    {
        if (initialized && api.SetLocalPosition != null) api.SetLocalPosition(ens, value);
    }

    //读取 localRotation
    internal static quaternion GetLocalRotation(EnsId ens)
    {
        return initialized && api.GetLocalRotation != null ? api.GetLocalRotation(ens) : new quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    }

    //写入 localRotation
    internal static void SetLocalRotation(EnsId ens, quaternion value)
    {
        if (initialized && api.SetLocalRotation != null) api.SetLocalRotation(ens, value);
    }

    //读取 localScale
    internal static vector3 GetLocalScale(EnsId ens)
    {
        return initialized && api.GetLocalScale != null ? api.GetLocalScale(ens) : new vector3(1.0f, 1.0f, 1.0f);
    }

    //写入 localScale
    internal static void SetLocalScale(EnsId ens, vector3 value)
    {
        if (initialized && api.SetLocalScale != null) api.SetLocalScale(ens, value);
    }

    //读取 worldPosition
    internal static vector3 GetWorldPosition(EnsId ens)
    {
        return initialized && api.GetWorldPosition != null ? api.GetWorldPosition(ens) : default;
    }

    //读取 worldRotation
    internal static quaternion GetWorldRotation(EnsId ens)
    {
        return initialized && api.GetWorldRotation != null ? api.GetWorldRotation(ens) : new quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    }
}

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct StaticMeshRendererBindApi
{
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> GetMesh;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr, byte> SetMesh;
    public delegate* unmanaged[Cdecl]<EnsId, uint> GetDrawQueue;
    public delegate* unmanaged[Cdecl]<EnsId, uint, void> SetDrawQueue;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetCastShadows;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetCastShadows;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetReceiveShadows;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetReceiveShadows;
}
#pragma warning restore CS0649

public sealed unsafe partial class StaticMeshRenderer
{
private static StaticMeshRendererBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 StaticMeshRenderer 函数表
    internal static void InitializeNativeApi(StaticMeshRendererBindApi value)
    {
        api = value;
        initialized = api.GetEnabled != null;
    }

    //读取 enabled
    internal static bool GetEnabled(EnsId ens)
    {
        return initialized && api.GetEnabled != null && api.GetEnabled(ens) != 0;
    }

    //写入 enabled
    internal static void SetEnabled(EnsId ens, bool value)
    {
        if (initialized && api.SetEnabled != null) api.SetEnabled(ens, value ? (byte)1 : (byte)0);
    }

    //读取 mesh
    internal static IntPtr GetMesh(EnsId ens)
    {
        return initialized && api.GetMesh != null ? api.GetMesh(ens) : IntPtr.Zero;
    }

    //写入 mesh
    internal static bool SetMesh(EnsId ens, IntPtr mesh)
    {
        if (!initialized || api.SetMesh == null) return false;
        return api.SetMesh(ens, mesh) != 0;
    }

    //读取 drawQueue
    internal static DrawQueue GetDrawQueue(EnsId ens)
    {
        return initialized && api.GetDrawQueue != null ? (DrawQueue)api.GetDrawQueue(ens) : DrawQueue.Opaque;
    }

    //写入 drawQueue
    internal static void SetDrawQueue(EnsId ens, DrawQueue value)
    {
        if (initialized && api.SetDrawQueue != null) api.SetDrawQueue(ens, (uint)value);
    }

    //读取 castShadows
    internal static bool GetCastShadows(EnsId ens)
    {
        return initialized && api.GetCastShadows != null && api.GetCastShadows(ens) != 0;
    }

    //写入 castShadows
    internal static void SetCastShadows(EnsId ens, bool value)
    {
        if (initialized && api.SetCastShadows != null) api.SetCastShadows(ens, value ? (byte)1 : (byte)0);
    }

    //读取 receiveShadows
    internal static bool GetReceiveShadows(EnsId ens)
    {
        return initialized && api.GetReceiveShadows != null && api.GetReceiveShadows(ens) != 0;
    }

    //写入 receiveShadows
    internal static void SetReceiveShadows(EnsId ens, bool value)
    {
        if (initialized && api.SetReceiveShadows != null) api.SetReceiveShadows(ens, value ? (byte)1 : (byte)0);
    }
}
