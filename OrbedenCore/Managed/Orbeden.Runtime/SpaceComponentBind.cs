using System;

namespace Orbeden;

internal static unsafe class SpaceComponentBind
{
    private static delegate* unmanaged<EnsId, EnsId> FuncGetParent;
    private static delegate* unmanaged<EnsId, EnsId, void> FuncSetParent;
    private static delegate* unmanaged<EnsId, vector3> FuncGetLocalPosition;
    private static delegate* unmanaged<EnsId, vector3, void> FuncSetLocalPosition;
    private static delegate* unmanaged<EnsId, quaternion> FuncGetLocalRotation;
    private static delegate* unmanaged<EnsId, quaternion, void> FuncSetLocalRotation;
    private static delegate* unmanaged<EnsId, vector3> FuncGetLocalScale;
    private static delegate* unmanaged<EnsId, vector3, void> FuncSetLocalScale;
    private static delegate* unmanaged<EnsId, vector3> FuncGetWorldPosition;
    private static delegate* unmanaged<EnsId, quaternion> FuncGetWorldRotation;
    private static bool initialized;

    //保存 C++ 传入的 SpaceComponent 函数表
    internal static void Initialize(IntPtr bindPointer)
    {
        if (bindPointer == IntPtr.Zero)
        {
            initialized = false;
            FuncGetParent = null;
            FuncSetParent = null;
            FuncGetLocalPosition = null;
            FuncSetLocalPosition = null;
            FuncGetLocalRotation = null;
            FuncSetLocalRotation = null;
            FuncGetLocalScale = null;
            FuncSetLocalScale = null;
            FuncGetWorldPosition = null;
            FuncGetWorldRotation = null;
            return;
        }

        void** functions = (void**)bindPointer;
        FuncGetParent = (delegate* unmanaged<EnsId, EnsId>)functions[0];
        FuncSetParent = (delegate* unmanaged<EnsId, EnsId, void>)functions[1];
        FuncGetLocalPosition = (delegate* unmanaged<EnsId, vector3>)functions[2];
        FuncSetLocalPosition = (delegate* unmanaged<EnsId, vector3, void>)functions[3];
        FuncGetLocalRotation = (delegate* unmanaged<EnsId, quaternion>)functions[4];
        FuncSetLocalRotation = (delegate* unmanaged<EnsId, quaternion, void>)functions[5];
        FuncGetLocalScale = (delegate* unmanaged<EnsId, vector3>)functions[6];
        FuncSetLocalScale = (delegate* unmanaged<EnsId, vector3, void>)functions[7];
        FuncGetWorldPosition = (delegate* unmanaged<EnsId, vector3>)functions[8];
        FuncGetWorldRotation = (delegate* unmanaged<EnsId, quaternion>)functions[9];
        initialized = true;
    }

    //读取 parent
    internal static EnsId GetParent(EnsId ens)
    {
        return initialized && FuncGetParent != null ? FuncGetParent(ens) : EnsId.Null;
    }

    //写入 parent
    internal static void SetParent(EnsId ens, EnsId parent)
    {
        if (initialized && FuncSetParent != null) FuncSetParent(ens, parent);
    }

    //读取 localPosition
    internal static vector3 GetLocalPosition(EnsId ens)
    {
        return initialized && FuncGetLocalPosition != null ? FuncGetLocalPosition(ens) : default;
    }

    //写入 localPosition
    internal static void SetLocalPosition(EnsId ens, vector3 value)
    {
        if (initialized && FuncSetLocalPosition != null) FuncSetLocalPosition(ens, value);
    }

    //读取 localRotation
    internal static quaternion GetLocalRotation(EnsId ens)
    {
        return initialized && FuncGetLocalRotation != null ? FuncGetLocalRotation(ens) : new quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    }

    //写入 localRotation
    internal static void SetLocalRotation(EnsId ens, quaternion value)
    {
        if (initialized && FuncSetLocalRotation != null) FuncSetLocalRotation(ens, value);
    }

    //读取 localScale
    internal static vector3 GetLocalScale(EnsId ens)
    {
        return initialized && FuncGetLocalScale != null ? FuncGetLocalScale(ens) : new vector3(1.0f, 1.0f, 1.0f);
    }

    //写入 localScale
    internal static void SetLocalScale(EnsId ens, vector3 value)
    {
        if (initialized && FuncSetLocalScale != null) FuncSetLocalScale(ens, value);
    }

    //读取 worldPosition
    internal static vector3 GetWorldPosition(EnsId ens)
    {
        return initialized && FuncGetWorldPosition != null ? FuncGetWorldPosition(ens) : default;
    }

    //读取 worldRotation
    internal static quaternion GetWorldRotation(EnsId ens)
    {
        return initialized && FuncGetWorldRotation != null ? FuncGetWorldRotation(ens) : new quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    }
}
