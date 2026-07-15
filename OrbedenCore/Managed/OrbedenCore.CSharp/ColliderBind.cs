using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ColliderBindApi
{
    public delegate* unmanaged[Cdecl]<EnsId, byte> HasComponent;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> AddComponent;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> GetComponent;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, uint> GetShape;
    public delegate* unmanaged[Cdecl]<EnsId, uint, void> SetShape;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetIsTrigger;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetIsTrigger;
    public delegate* unmanaged[Cdecl]<EnsId, vector3> GetCenter;
    public delegate* unmanaged[Cdecl]<EnsId, vector3, void> SetCenter;
    public delegate* unmanaged[Cdecl]<EnsId, vector3> GetHalfExtents;
    public delegate* unmanaged[Cdecl]<EnsId, vector3, void> SetHalfExtents;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetRadius;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetRadius;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetHalfHeight;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetHalfHeight;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> GetMesh;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr, byte> SetMesh;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetStaticFriction;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetStaticFriction;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetDynamicFriction;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetDynamicFriction;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetRestitution;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetRestitution;
    public delegate* unmanaged[Cdecl]<EnsId, uint> GetCollisionLayer;
    public delegate* unmanaged[Cdecl]<EnsId, uint, void> SetCollisionLayer;
    public delegate* unmanaged[Cdecl]<EnsId, uint> GetCollisionMask;
    public delegate* unmanaged[Cdecl]<EnsId, uint, void> SetCollisionMask;
}
#pragma warning restore CS0649

internal static unsafe class ColliderBind
{
    private static ColliderBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 Collider 函数表
    internal static void Initialize(ColliderBindApi value)
    {
        api = value;
        initialized = api.HasComponent != null;
    }

    //判断 Ens 是否拥有 Collider
    internal static bool HasComponent(EnsId ens)
    {
        return initialized && api.HasComponent != null && api.HasComponent(ens) != 0;
    }

    //添加 Collider
    internal static IntPtr AddComponent(EnsId ens)
    {
        return initialized && api.AddComponent != null ? api.AddComponent(ens) : IntPtr.Zero;
    }

    //获取 Collider 指针
    internal static IntPtr GetComponent(EnsId ens)
    {
        return initialized && api.GetComponent != null ? api.GetComponent(ens) : IntPtr.Zero;
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

    //读取 shape
    internal static ColliderShape GetShape(EnsId ens)
    {
        return initialized && api.GetShape != null ? (ColliderShape)api.GetShape(ens) : ColliderShape.Box;
    }

    //写入 shape
    internal static void SetShape(EnsId ens, ColliderShape value)
    {
        if (initialized && api.SetShape != null) api.SetShape(ens, (uint)value);
    }

    //读取 isTrigger
    internal static bool GetIsTrigger(EnsId ens)
    {
        return initialized && api.GetIsTrigger != null && api.GetIsTrigger(ens) != 0;
    }

    //写入 isTrigger
    internal static void SetIsTrigger(EnsId ens, bool value)
    {
        if (initialized && api.SetIsTrigger != null) api.SetIsTrigger(ens, value ? (byte)1 : (byte)0);
    }

    //读取 center
    internal static vector3 GetCenter(EnsId ens)
    {
        return initialized && api.GetCenter != null ? api.GetCenter(ens) : default;
    }

    //写入 center
    internal static void SetCenter(EnsId ens, vector3 value)
    {
        if (initialized && api.SetCenter != null) api.SetCenter(ens, value);
    }

    //读取 halfExtents
    internal static vector3 GetHalfExtents(EnsId ens)
    {
        return initialized && api.GetHalfExtents != null ? api.GetHalfExtents(ens) : default;
    }

    //写入 halfExtents
    internal static void SetHalfExtents(EnsId ens, vector3 value)
    {
        if (initialized && api.SetHalfExtents != null) api.SetHalfExtents(ens, value);
    }

    //读取 radius
    internal static float GetRadius(EnsId ens)
    {
        return initialized && api.GetRadius != null ? api.GetRadius(ens) : 0.0f;
    }

    //写入 radius
    internal static void SetRadius(EnsId ens, float value)
    {
        if (initialized && api.SetRadius != null) api.SetRadius(ens, value);
    }

    //读取 halfHeight
    internal static float GetHalfHeight(EnsId ens)
    {
        return initialized && api.GetHalfHeight != null ? api.GetHalfHeight(ens) : 0.0f;
    }

    //写入 halfHeight
    internal static void SetHalfHeight(EnsId ens, float value)
    {
        if (initialized && api.SetHalfHeight != null) api.SetHalfHeight(ens, value);
    }

    //读取 mesh
    internal static IntPtr GetMesh(EnsId ens)
    {
        return initialized && api.GetMesh != null ? api.GetMesh(ens) : IntPtr.Zero;
    }

    //写入 mesh
    internal static bool SetMesh(EnsId ens, IntPtr mesh)
    {
        return initialized && api.SetMesh != null && api.SetMesh(ens, mesh) != 0;
    }

    //读取 staticFriction
    internal static float GetStaticFriction(EnsId ens)
    {
        return initialized && api.GetStaticFriction != null ? api.GetStaticFriction(ens) : 0.0f;
    }

    //写入 staticFriction
    internal static void SetStaticFriction(EnsId ens, float value)
    {
        if (initialized && api.SetStaticFriction != null) api.SetStaticFriction(ens, value);
    }

    //读取 dynamicFriction
    internal static float GetDynamicFriction(EnsId ens)
    {
        return initialized && api.GetDynamicFriction != null ? api.GetDynamicFriction(ens) : 0.0f;
    }

    //写入 dynamicFriction
    internal static void SetDynamicFriction(EnsId ens, float value)
    {
        if (initialized && api.SetDynamicFriction != null) api.SetDynamicFriction(ens, value);
    }

    //读取 restitution
    internal static float GetRestitution(EnsId ens)
    {
        return initialized && api.GetRestitution != null ? api.GetRestitution(ens) : 0.0f;
    }

    //写入 restitution
    internal static void SetRestitution(EnsId ens, float value)
    {
        if (initialized && api.SetRestitution != null) api.SetRestitution(ens, value);
    }

    //读取 collisionLayer
    internal static uint GetCollisionLayer(EnsId ens)
    {
        return initialized && api.GetCollisionLayer != null ? api.GetCollisionLayer(ens) : 0;
    }

    //写入 collisionLayer
    internal static void SetCollisionLayer(EnsId ens, uint value)
    {
        if (initialized && api.SetCollisionLayer != null) api.SetCollisionLayer(ens, value);
    }

    //读取 collisionMask
    internal static uint GetCollisionMask(EnsId ens)
    {
        return initialized && api.GetCollisionMask != null ? api.GetCollisionMask(ens) : 0;
    }

    //写入 collisionMask
    internal static void SetCollisionMask(EnsId ens, uint value)
    {
        if (initialized && api.SetCollisionMask != null) api.SetCollisionMask(ens, value);
    }
}
