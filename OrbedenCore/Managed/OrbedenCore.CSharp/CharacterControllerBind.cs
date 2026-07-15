using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct CharacterControllerBindApi
{
    public delegate* unmanaged[Cdecl]<EnsId, byte> HasComponent;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> AddComponent;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> GetComponent;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetEnabled;
    public delegate* unmanaged[Cdecl]<EnsId, uint> GetShape;
    public delegate* unmanaged[Cdecl]<EnsId, uint, void> SetShape;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetRadius;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetRadius;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetHeight;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetHeight;
    public delegate* unmanaged[Cdecl]<EnsId, vector3> GetHalfExtents;
    public delegate* unmanaged[Cdecl]<EnsId, vector3, void> SetHalfExtents;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetStepOffset;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetStepOffset;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetContactOffset;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetContactOffset;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetSlopeLimit;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetSlopeLimit;
    public delegate* unmanaged[Cdecl]<EnsId, float> GetMinMoveDistance;
    public delegate* unmanaged[Cdecl]<EnsId, float, void> SetMinMoveDistance;
    public delegate* unmanaged[Cdecl]<EnsId, uint> GetCollisionLayer;
    public delegate* unmanaged[Cdecl]<EnsId, uint, void> SetCollisionLayer;
    public delegate* unmanaged[Cdecl]<EnsId, uint> GetCollisionMask;
    public delegate* unmanaged[Cdecl]<EnsId, uint, void> SetCollisionMask;
}
#pragma warning restore CS0649

internal static unsafe class CharacterControllerBind
{
    private static CharacterControllerBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 CharacterController 函数表
    internal static void Initialize(CharacterControllerBindApi value)
    {
        api = value;
        initialized = api.HasComponent != null;
    }

    //判断 Ens 是否拥有 CharacterController
    internal static bool HasComponent(EnsId ens)
    {
        return initialized && api.HasComponent != null && api.HasComponent(ens) != 0;
    }

    //添加 CharacterController
    internal static IntPtr AddComponent(EnsId ens)
    {
        return initialized && api.AddComponent != null ? api.AddComponent(ens) : IntPtr.Zero;
    }

    //获取 CharacterController 指针
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
    internal static CharacterControllerShape GetShape(EnsId ens)
    {
        return initialized && api.GetShape != null ? (CharacterControllerShape)api.GetShape(ens) : CharacterControllerShape.Capsule;
    }

    //写入 shape
    internal static void SetShape(EnsId ens, CharacterControllerShape value)
    {
        if (initialized && api.SetShape != null) api.SetShape(ens, (uint)value);
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

    //读取 height
    internal static float GetHeight(EnsId ens)
    {
        return initialized && api.GetHeight != null ? api.GetHeight(ens) : 0.0f;
    }

    //写入 height
    internal static void SetHeight(EnsId ens, float value)
    {
        if (initialized && api.SetHeight != null) api.SetHeight(ens, value);
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

    //读取 stepOffset
    internal static float GetStepOffset(EnsId ens)
    {
        return initialized && api.GetStepOffset != null ? api.GetStepOffset(ens) : 0.0f;
    }

    //写入 stepOffset
    internal static void SetStepOffset(EnsId ens, float value)
    {
        if (initialized && api.SetStepOffset != null) api.SetStepOffset(ens, value);
    }

    //读取 contactOffset
    internal static float GetContactOffset(EnsId ens)
    {
        return initialized && api.GetContactOffset != null ? api.GetContactOffset(ens) : 0.0f;
    }

    //写入 contactOffset
    internal static void SetContactOffset(EnsId ens, float value)
    {
        if (initialized && api.SetContactOffset != null) api.SetContactOffset(ens, value);
    }

    //读取 slopeLimit
    internal static float GetSlopeLimit(EnsId ens)
    {
        return initialized && api.GetSlopeLimit != null ? api.GetSlopeLimit(ens) : 0.0f;
    }

    //写入 slopeLimit
    internal static void SetSlopeLimit(EnsId ens, float value)
    {
        if (initialized && api.SetSlopeLimit != null) api.SetSlopeLimit(ens, value);
    }

    //读取 minMoveDistance
    internal static float GetMinMoveDistance(EnsId ens)
    {
        return initialized && api.GetMinMoveDistance != null ? api.GetMinMoveDistance(ens) : 0.0f;
    }

    //写入 minMoveDistance
    internal static void SetMinMoveDistance(EnsId ens, float value)
    {
        if (initialized && api.SetMinMoveDistance != null) api.SetMinMoveDistance(ens, value);
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
