using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct SpaceComponentBindApi
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

internal static unsafe class SpaceComponentBind
{
    private static SpaceComponentBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 SpaceComponent 函数表
    internal static void Initialize(SpaceComponentBindApi value)
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
