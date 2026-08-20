using System;

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>刚体运动类型。</summary>
public enum PhysicsBodyType : uint
{
    Static = 0,
    Dynamic = 1,
    Kinematic = 2,
}

/// <summary>角色控制器几何类型。</summary>
public enum CharacterControllerShape : uint
{
    Capsule = 0,
    Box = 1,
}

/// <summary>刚体轴锁定位。</summary>
[Flags]
public enum PhysicsLockFlags : uint
{
    None = 0,
    PositionX = 1u << 0,
    PositionY = 1u << 1,
    PositionZ = 1u << 2,
    RotationX = 1u << 3,
    RotationY = 1u << 4,
    RotationZ = 1u << 5,
}

/// <summary>刚体组件包装。</summary>
[UniqueComponent]
public sealed partial class RigidBody : Component
{
    /// <summary>创建刚体组件包装。</summary>
    internal RigidBody(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    //从原生指针获取 RigidBody 包装
    internal static RigidBody? FromNative(Ens ens, IntPtr pointer)
    {
        return Object.FromNative(pointer, value => new RigidBody(ens, value));
    }

    /// <summary>是否启用刚体。</summary>
    public bool enabled
    {
        get => GetEnabled(Ens.Id);
        set => SetEnabled(Ens.Id, value);
    }

    /// <summary>刚体运动类型。</summary>
    public PhysicsBodyType bodyType
    {
        get => GetBodyType(Ens.Id);
        set => SetBodyType(Ens.Id, value);
    }

    /// <summary>动态刚体质量。</summary>
    public float mass
    {
        get => GetMass(Ens.Id);
        set => SetMass(Ens.Id, value);
    }

    /// <summary>是否使用场景重力。</summary>
    public bool useGravity
    {
        get => GetUseGravity(Ens.Id);
        set => SetUseGravity(Ens.Id, value);
    }

    /// <summary>线性阻尼。</summary>
    public float linearDamping
    {
        get => GetLinearDamping(Ens.Id);
        set => SetLinearDamping(Ens.Id, value);
    }

    /// <summary>角阻尼。</summary>
    public float angularDamping
    {
        get => GetAngularDamping(Ens.Id);
        set => SetAngularDamping(Ens.Id, value);
    }

    /// <summary>世界空间线速度。</summary>
    public vector3 linearVelocity
    {
        get => GetLinearVelocity(Ens.Id);
        set => SetLinearVelocity(Ens.Id, value);
    }

    /// <summary>世界空间角速度。</summary>
    public vector3 angularVelocity
    {
        get => GetAngularVelocity(Ens.Id);
        set => SetAngularVelocity(Ens.Id, value);
    }

    /// <summary>是否启用连续碰撞检测。</summary>
    public bool continuousCollisionDetection
    {
        get => GetContinuousCollisionDetection(Ens.Id);
        set => SetContinuousCollisionDetection(Ens.Id, value);
    }

    /// <summary>刚体轴锁定位。</summary>
    public PhysicsLockFlags lockFlags
    {
        get => GetLockFlags(Ens.Id);
        set => SetLockFlags(Ens.Id, value);
    }
}

/// <summary>碰撞体组件的抽象基类。</summary>
public abstract partial class Collider : Component
{
    /// <summary>创建碰撞体组件包装。</summary>
    internal Collider(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    /// <summary>是否启用碰撞体。</summary>
    public bool enabled
    {
        get => GetEnabled(NativePtr);
        set => SetEnabled(NativePtr, value);
    }

    /// <summary>是否作为触发器。</summary>
    public bool isTrigger
    {
        get => GetIsTrigger(NativePtr);
        set => SetIsTrigger(NativePtr, value);
    }

    /// <summary>相对实体原点的碰撞中心。</summary>
    public vector3 center
    {
        get => GetCenter(NativePtr);
        set => SetCenter(NativePtr, value);
    }

    /// <summary>静摩擦系数。</summary>
    public float staticFriction
    {
        get => GetStaticFriction(NativePtr);
        set => SetStaticFriction(NativePtr, value);
    }

    /// <summary>动摩擦系数。</summary>
    public float dynamicFriction
    {
        get => GetDynamicFriction(NativePtr);
        set => SetDynamicFriction(NativePtr, value);
    }

    /// <summary>弹性系数。</summary>
    public float restitution
    {
        get => GetRestitution(NativePtr);
        set => SetRestitution(NativePtr, value);
    }

    /// <summary>碰撞层位。</summary>
    public uint collisionLayer
    {
        get => GetCollisionLayer(NativePtr);
        set => SetCollisionLayer(NativePtr, value);
    }

    /// <summary>允许碰撞的层掩码。</summary>
    public uint collisionMask
    {
        get => GetCollisionMask(NativePtr);
        set => SetCollisionMask(NativePtr, value);
    }
}

/// <summary>盒形碰撞体组件。</summary>
public sealed partial class BoxCollider : Collider
{
    internal BoxCollider(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    /// <summary>盒体半尺寸。</summary>
    public vector3 halfExtents
    {
        get => GetHalfExtents(NativePtr);
        set => SetHalfExtents(NativePtr, value);
    }
}

/// <summary>球形碰撞体组件。</summary>
public sealed partial class SphereCollider : Collider
{
    internal SphereCollider(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    /// <summary>球体半径。</summary>
    public float radius
    {
        get => GetRadius(NativePtr);
        set => SetRadius(NativePtr, value);
    }
}

/// <summary>胶囊碰撞体组件。</summary>
public sealed partial class CapsuleCollider : Collider
{
    internal CapsuleCollider(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    /// <summary>胶囊体半径。</summary>
    public float radius
    {
        get => GetRadius(NativePtr);
        set => SetRadius(NativePtr, value);
    }

    /// <summary>胶囊体圆柱部分半高度。</summary>
    public float halfHeight
    {
        get => GetHalfHeight(NativePtr);
        set => SetHalfHeight(NativePtr, value);
    }
}

/// <summary>凸包网格碰撞体组件。</summary>
public sealed partial class ConvexMeshCollider : Collider
{
    internal ConvexMeshCollider(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    /// <summary>凸包网格资源。</summary>
    public Mesh? mesh
    {
        get => Mesh.FromNative(GetMesh(NativePtr));
        set => SetMesh(NativePtr, value?.NativePtr ?? IntPtr.Zero);
    }
}

/// <summary>三角网格碰撞体组件。</summary>
public sealed partial class TriangleMeshCollider : Collider
{
    internal TriangleMeshCollider(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    /// <summary>三角网格资源。</summary>
    public Mesh? mesh
    {
        get => Mesh.FromNative(GetMesh(NativePtr));
        set => SetMesh(NativePtr, value?.NativePtr ?? IntPtr.Zero);
    }
}

/// <summary>角色控制器组件包装。</summary>
[UniqueComponent]
public sealed partial class CharacterController : Component
{
    /// <summary>创建角色控制器组件包装。</summary>
    internal CharacterController(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    //从原生指针获取 CharacterController 包装
    internal static CharacterController? FromNative(Ens ens, IntPtr pointer)
    {
        return Object.FromNative(pointer, value => new CharacterController(ens, value));
    }

    /// <summary>是否启用角色控制器。</summary>
    public bool enabled
    {
        get => GetEnabled(Ens.Id);
        set => SetEnabled(Ens.Id, value);
    }

    /// <summary>角色控制器几何类型。</summary>
    public CharacterControllerShape shape
    {
        get => GetShape(Ens.Id);
        set => SetShape(Ens.Id, value);
    }

    /// <summary>胶囊控制器半径。</summary>
    public float radius
    {
        get => GetRadius(Ens.Id);
        set => SetRadius(Ens.Id, value);
    }

    /// <summary>胶囊控制器圆柱部分高度。</summary>
    public float height
    {
        get => GetHeight(Ens.Id);
        set => SetHeight(Ens.Id, value);
    }

    /// <summary>盒形控制器半尺寸。</summary>
    public vector3 halfExtents
    {
        get => GetHalfExtents(Ens.Id);
        set => SetHalfExtents(Ens.Id, value);
    }

    /// <summary>允许自动跨越的台阶高度。</summary>
    public float stepOffset
    {
        get => GetStepOffset(Ens.Id);
        set => SetStepOffset(Ens.Id, value);
    }

    /// <summary>控制器表面接触偏移。</summary>
    public float contactOffset
    {
        get => GetContactOffset(Ens.Id);
        set => SetContactOffset(Ens.Id, value);
    }

    /// <summary>可攀爬斜坡的法线阈值。</summary>
    public float slopeLimit
    {
        get => GetSlopeLimit(Ens.Id);
        set => SetSlopeLimit(Ens.Id, value);
    }

    /// <summary>忽略的最小移动距离。</summary>
    public float minMoveDistance
    {
        get => GetMinMoveDistance(Ens.Id);
        set => SetMinMoveDistance(Ens.Id, value);
    }

    /// <summary>控制器碰撞层位。</summary>
    public uint collisionLayer
    {
        get => GetCollisionLayer(Ens.Id);
        set => SetCollisionLayer(Ens.Id, value);
    }

    /// <summary>允许碰撞的层掩码。</summary>
    public uint collisionMask
    {
        get => GetCollisionMask(Ens.Id);
        set => SetCollisionMask(Ens.Id, value);
    }
}

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
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

public sealed unsafe partial class RigidBody
{
private static RigidBodyBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 RigidBody 函数表
    internal static void InitializeNativeApi(RigidBodyBindApi value)
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

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ColliderBindApi
{
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> AddBoxCollider;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> AddSphereCollider;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> AddCapsuleCollider;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> AddConvexMeshCollider;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> AddTriangleMeshCollider;
    public delegate* unmanaged[Cdecl]<EnsId, int> GetColliderCount;
    public delegate* unmanaged[Cdecl]<EnsId, int, IntPtr> GetColliderAt;
    public delegate* unmanaged[Cdecl]<IntPtr, uint> GetGeometryType;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> GetEnabled;
    public delegate* unmanaged[Cdecl]<IntPtr, byte, void> SetEnabled;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> GetIsTrigger;
    public delegate* unmanaged[Cdecl]<IntPtr, byte, void> SetIsTrigger;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3> GetCenter;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3, void> SetCenter;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3> GetHalfExtents;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3, void> SetHalfExtents;
    public delegate* unmanaged[Cdecl]<IntPtr, float> GetRadius;
    public delegate* unmanaged[Cdecl]<IntPtr, float, void> SetRadius;
    public delegate* unmanaged[Cdecl]<IntPtr, float> GetHalfHeight;
    public delegate* unmanaged[Cdecl]<IntPtr, float, void> SetHalfHeight;
    public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> GetMesh;
    public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, byte> SetMesh;
    public delegate* unmanaged[Cdecl]<IntPtr, float> GetStaticFriction;
    public delegate* unmanaged[Cdecl]<IntPtr, float, void> SetStaticFriction;
    public delegate* unmanaged[Cdecl]<IntPtr, float> GetDynamicFriction;
    public delegate* unmanaged[Cdecl]<IntPtr, float, void> SetDynamicFriction;
    public delegate* unmanaged[Cdecl]<IntPtr, float> GetRestitution;
    public delegate* unmanaged[Cdecl]<IntPtr, float, void> SetRestitution;
    public delegate* unmanaged[Cdecl]<IntPtr, uint> GetCollisionLayer;
    public delegate* unmanaged[Cdecl]<IntPtr, uint, void> SetCollisionLayer;
    public delegate* unmanaged[Cdecl]<IntPtr, uint> GetCollisionMask;
    public delegate* unmanaged[Cdecl]<IntPtr, uint, void> SetCollisionMask;
}
#pragma warning restore CS0649

internal enum ColliderGeometryType : uint
{
    Box = 0,
    Sphere = 1,
    Capsule = 2,
    ConvexMesh = 3,
    TriangleMesh = 4,
}

public abstract unsafe partial class Collider
{
    private static ColliderBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 Collider 函数表
    internal static void InitializeNativeApi(ColliderBindApi value)
    {
        api = value;
        initialized = api.GetColliderCount != null;
    }

    //枚举 Ens 的 Collider 实例
    internal static Collider[] GetComponents(Ens ens)
    {
        if (!initialized || api.GetColliderCount == null || api.GetColliderAt == null) return [];

        int count = Math.Max(0, api.GetColliderCount(ens.Id));
        Collider[] result = new Collider[count];
        int index = 0;
        for (int i = 0; i < count; ++i)
        {
            Collider? collider = FromNative(ens, api.GetColliderAt(ens.Id, i));
            if (collider != null) result[index++] = collider;
        }

        if (index == result.Length) return result;
        Array.Resize(ref result, index);
        return result;
    }

    //添加 BoxCollider
    internal static BoxCollider? AddBoxCollider(Ens ens)
    {
        return FromNative(ens, initialized && api.AddBoxCollider != null ? api.AddBoxCollider(ens.Id) : IntPtr.Zero) as BoxCollider;
    }

    //添加 SphereCollider
    internal static SphereCollider? AddSphereCollider(Ens ens)
    {
        return FromNative(ens, initialized && api.AddSphereCollider != null ? api.AddSphereCollider(ens.Id) : IntPtr.Zero) as SphereCollider;
    }

    //添加 CapsuleCollider
    internal static CapsuleCollider? AddCapsuleCollider(Ens ens)
    {
        return FromNative(ens, initialized && api.AddCapsuleCollider != null ? api.AddCapsuleCollider(ens.Id) : IntPtr.Zero) as CapsuleCollider;
    }

    //添加 ConvexMeshCollider
    internal static ConvexMeshCollider? AddConvexMeshCollider(Ens ens)
    {
        return FromNative(ens, initialized && api.AddConvexMeshCollider != null ? api.AddConvexMeshCollider(ens.Id) : IntPtr.Zero) as ConvexMeshCollider;
    }

    //添加 TriangleMeshCollider
    internal static TriangleMeshCollider? AddTriangleMeshCollider(Ens ens)
    {
        return FromNative(ens, initialized && api.AddTriangleMeshCollider != null ? api.AddTriangleMeshCollider(ens.Id) : IntPtr.Zero) as TriangleMeshCollider;
    }

    //按原生 Collider 类型创建包装
    internal static Collider? FromNative(Ens ens, IntPtr pointer)
    {
        if (pointer == IntPtr.Zero) return null;
        return GetGeometryType(pointer) switch
        {
            ColliderGeometryType.Box => Object.FromNative(pointer, value => new BoxCollider(ens, value)),
            ColliderGeometryType.Sphere => Object.FromNative(pointer, value => new SphereCollider(ens, value)),
            ColliderGeometryType.Capsule => Object.FromNative(pointer, value => new CapsuleCollider(ens, value)),
            ColliderGeometryType.ConvexMesh => Object.FromNative(pointer, value => new ConvexMeshCollider(ens, value)),
            ColliderGeometryType.TriangleMesh => Object.FromNative(pointer, value => new TriangleMeshCollider(ens, value)),
            _ => null,
        };
    }

    //读取 Collider 类型
    private static ColliderGeometryType GetGeometryType(IntPtr collider)
    {
        return initialized && api.GetGeometryType != null ? (ColliderGeometryType)api.GetGeometryType(collider) : ColliderGeometryType.Box;
    }

    //读取 enabled
    protected static bool GetEnabled(IntPtr collider)
    {
        return initialized && api.GetEnabled != null && api.GetEnabled(collider) != 0;
    }

    //写入 enabled
    protected static void SetEnabled(IntPtr collider, bool value)
    {
        if (initialized && api.SetEnabled != null) api.SetEnabled(collider, value ? (byte)1 : (byte)0);
    }

    //读取 isTrigger
    protected static bool GetIsTrigger(IntPtr collider)
    {
        return initialized && api.GetIsTrigger != null && api.GetIsTrigger(collider) != 0;
    }

    //写入 isTrigger
    protected static void SetIsTrigger(IntPtr collider, bool value)
    {
        if (initialized && api.SetIsTrigger != null) api.SetIsTrigger(collider, value ? (byte)1 : (byte)0);
    }

    //读取 center
    protected static vector3 GetCenter(IntPtr collider)
    {
        return initialized && api.GetCenter != null ? api.GetCenter(collider) : default;
    }

    //写入 center
    protected static void SetCenter(IntPtr collider, vector3 value)
    {
        if (initialized && api.SetCenter != null) api.SetCenter(collider, value);
    }

    //读取 halfExtents
    protected static vector3 GetHalfExtents(IntPtr collider)
    {
        return initialized && api.GetHalfExtents != null ? api.GetHalfExtents(collider) : default;
    }

    //写入 halfExtents
    protected static void SetHalfExtents(IntPtr collider, vector3 value)
    {
        if (initialized && api.SetHalfExtents != null) api.SetHalfExtents(collider, value);
    }

    //读取 radius
    protected static float GetRadius(IntPtr collider)
    {
        return initialized && api.GetRadius != null ? api.GetRadius(collider) : 0.0f;
    }

    //写入 radius
    protected static void SetRadius(IntPtr collider, float value)
    {
        if (initialized && api.SetRadius != null) api.SetRadius(collider, value);
    }

    //读取 halfHeight
    protected static float GetHalfHeight(IntPtr collider)
    {
        return initialized && api.GetHalfHeight != null ? api.GetHalfHeight(collider) : 0.0f;
    }

    //写入 halfHeight
    protected static void SetHalfHeight(IntPtr collider, float value)
    {
        if (initialized && api.SetHalfHeight != null) api.SetHalfHeight(collider, value);
    }

    //读取 mesh
    protected static IntPtr GetMesh(IntPtr collider)
    {
        return initialized && api.GetMesh != null ? api.GetMesh(collider) : IntPtr.Zero;
    }

    //写入 mesh
    protected static bool SetMesh(IntPtr collider, IntPtr mesh)
    {
        return initialized && api.SetMesh != null && api.SetMesh(collider, mesh) != 0;
    }

    //读取 staticFriction
    protected static float GetStaticFriction(IntPtr collider)
    {
        return initialized && api.GetStaticFriction != null ? api.GetStaticFriction(collider) : 0.0f;
    }

    //写入 staticFriction
    protected static void SetStaticFriction(IntPtr collider, float value)
    {
        if (initialized && api.SetStaticFriction != null) api.SetStaticFriction(collider, value);
    }

    //读取 dynamicFriction
    protected static float GetDynamicFriction(IntPtr collider)
    {
        return initialized && api.GetDynamicFriction != null ? api.GetDynamicFriction(collider) : 0.0f;
    }

    //写入 dynamicFriction
    protected static void SetDynamicFriction(IntPtr collider, float value)
    {
        if (initialized && api.SetDynamicFriction != null) api.SetDynamicFriction(collider, value);
    }

    //读取 restitution
    protected static float GetRestitution(IntPtr collider)
    {
        return initialized && api.GetRestitution != null ? api.GetRestitution(collider) : 0.0f;
    }

    //写入 restitution
    protected static void SetRestitution(IntPtr collider, float value)
    {
        if (initialized && api.SetRestitution != null) api.SetRestitution(collider, value);
    }

    //读取 collisionLayer
    protected static uint GetCollisionLayer(IntPtr collider)
    {
        return initialized && api.GetCollisionLayer != null ? api.GetCollisionLayer(collider) : 0;
    }

    //写入 collisionLayer
    protected static void SetCollisionLayer(IntPtr collider, uint value)
    {
        if (initialized && api.SetCollisionLayer != null) api.SetCollisionLayer(collider, value);
    }

    //读取 collisionMask
    protected static uint GetCollisionMask(IntPtr collider)
    {
        return initialized && api.GetCollisionMask != null ? api.GetCollisionMask(collider) : 0;
    }

    //写入 collisionMask
    protected static void SetCollisionMask(IntPtr collider, uint value)
    {
        if (initialized && api.SetCollisionMask != null) api.SetCollisionMask(collider, value);
    }
}

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
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

public sealed unsafe partial class CharacterController
{
private static CharacterControllerBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 CharacterController 函数表
    internal static void InitializeNativeApi(CharacterControllerBindApi value)
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
