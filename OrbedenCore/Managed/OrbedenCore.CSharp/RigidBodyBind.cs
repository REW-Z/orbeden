using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct RigidBodyBindApi
{
    public delegate* unmanaged[Cdecl]<EnsId, byte> HasComponent;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> AddComponent;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> GetComponent;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, uint> GetBodyType;
    public delegate* unmanaged[Cdecl]<EnsId, uint, void> SetBodyType;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetMass;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetMass;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetUseGravity;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetUseGravity;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetLinearDamping;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetLinearDamping;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetAngularDamping;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetAngularDamping;
    public delegate* unmanaged[Cdecl]<EnsId, vector3> GetLinearVelocity;
    public delegate* unmanaged[Cdecl]<EnsId, vector3, void> SetLinearVelocity;
    public delegate* unmanaged[Cdecl]<EnsId, vector3> GetAngularVelocity;
    public delegate* unmanaged[Cdecl]<EnsId, vector3, void> SetAngularVelocity;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetContinuousCollisionDetection;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetContinuousCollisionDetection;
    public delegate* unmanaged[Cdecl]<EnsId, uint> GetLockFlags;
    public delegate* unmanaged[Cdecl]<EnsId, uint, void> SetLockFlags;
}
#pragma warning restore CS0649

internal static unsafe class RigidBodyBind
{
    private static RigidBodyBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 RigidBody 函数表
    internal static void Initialize(RigidBodyBindApi value)
    {
        api = value;
        initialized = api.HasComponent != null;
    }

    //判断 Ens 是否拥有 RigidBody
    internal static bool HasComponent(EnsId ens)
    {
        return initialized && api.HasComponent != null && api.HasComponent(ens) != 0;
    }

    //添加 RigidBody
    internal static IntPtr AddComponent(EnsId ens)
    {
        return initialized && api.AddComponent != null ? api.AddComponent(ens) : IntPtr.Zero;
    }

    //获取 RigidBody 指针
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

    //读取 bodyType
    internal static PhysicsBodyType GetBodyType(EnsId ens)
    {
        return initialized && api.GetBodyType != null ? (PhysicsBodyType)api.GetBodyType(ens) : PhysicsBodyType.Static;
    }

    //写入 bodyType
    internal static void SetBodyType(EnsId ens, PhysicsBodyType value)
    {
        if (initialized && api.SetBodyType != null) api.SetBodyType(ens, (uint)value);
    }

    //读取 mass
    internal static float GetMass(EnsId ens)
    {
        return initialized && api.GetMass != null ? api.GetMass(ens) : 0.0f;
    }

    //写入 mass
    internal static void SetMass(EnsId ens, float value)
    {
        if (initialized && api.SetMass != null) api.SetMass(ens, value);
    }

    //读取 useGravity
    internal static bool GetUseGravity(EnsId ens)
    {
        return initialized && api.GetUseGravity != null && api.GetUseGravity(ens) != 0;
    }

    //写入 useGravity
    internal static void SetUseGravity(EnsId ens, bool value)
    {
        if (initialized && api.SetUseGravity != null) api.SetUseGravity(ens, value ? (byte)1 : (byte)0);
    }

    //读取 linearDamping
    internal static float GetLinearDamping(EnsId ens)
    {
        return initialized && api.GetLinearDamping != null ? api.GetLinearDamping(ens) : 0.0f;
    }

    //写入 linearDamping
    internal static void SetLinearDamping(EnsId ens, float value)
    {
        if (initialized && api.SetLinearDamping != null) api.SetLinearDamping(ens, value);
    }

    //读取 angularDamping
    internal static float GetAngularDamping(EnsId ens)
    {
        return initialized && api.GetAngularDamping != null ? api.GetAngularDamping(ens) : 0.0f;
    }

    //写入 angularDamping
    internal static void SetAngularDamping(EnsId ens, float value)
    {
        if (initialized && api.SetAngularDamping != null) api.SetAngularDamping(ens, value);
    }

    //读取 linearVelocity
    internal static vector3 GetLinearVelocity(EnsId ens)
    {
        return initialized && api.GetLinearVelocity != null ? api.GetLinearVelocity(ens) : default;
    }

    //写入 linearVelocity
    internal static void SetLinearVelocity(EnsId ens, vector3 value)
    {
        if (initialized && api.SetLinearVelocity != null) api.SetLinearVelocity(ens, value);
    }

    //读取 angularVelocity
    internal static vector3 GetAngularVelocity(EnsId ens)
    {
        return initialized && api.GetAngularVelocity != null ? api.GetAngularVelocity(ens) : default;
    }

    //写入 angularVelocity
    internal static void SetAngularVelocity(EnsId ens, vector3 value)
    {
        if (initialized && api.SetAngularVelocity != null) api.SetAngularVelocity(ens, value);
    }

    //读取 continuousCollisionDetection
    internal static bool GetContinuousCollisionDetection(EnsId ens)
    {
        return initialized && api.GetContinuousCollisionDetection != null && api.GetContinuousCollisionDetection(ens) != 0;
    }

    //写入 continuousCollisionDetection
    internal static void SetContinuousCollisionDetection(EnsId ens, bool value)
    {
        if (initialized && api.SetContinuousCollisionDetection != null)
        {
            api.SetContinuousCollisionDetection(ens, value ? (byte)1 : (byte)0);
        }
    }

    //读取 lockFlags
    internal static PhysicsLockFlags GetLockFlags(EnsId ens)
    {
        return initialized && api.GetLockFlags != null ? (PhysicsLockFlags)api.GetLockFlags(ens) : PhysicsLockFlags.None;
    }

    //写入 lockFlags
    internal static void SetLockFlags(EnsId ens, PhysicsLockFlags value)
    {
        if (initialized && api.SetLockFlags != null) api.SetLockFlags(ens, (uint)value);
    }
}
