using System;

namespace Orbeden;

/// <summary>刚体运动类型。</summary>
public enum PhysicsBodyType : uint
{
    Static = 0,
    Dynamic = 1,
    Kinematic = 2,
}

/// <summary>碰撞体几何类型。</summary>
public enum ColliderShape : uint
{
    Box = 0,
    Sphere = 1,
    Capsule = 2,
    ConvexMesh = 3,
    TriangleMesh = 4,
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
public sealed class RigidBody : Component
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
        get => RigidBodyBind.GetEnabled(Ens.Id);
        set => RigidBodyBind.SetEnabled(Ens.Id, value);
    }

    /// <summary>刚体运动类型。</summary>
    public PhysicsBodyType bodyType
    {
        get => RigidBodyBind.GetBodyType(Ens.Id);
        set => RigidBodyBind.SetBodyType(Ens.Id, value);
    }

    /// <summary>动态刚体质量。</summary>
    public float mass
    {
        get => RigidBodyBind.GetMass(Ens.Id);
        set => RigidBodyBind.SetMass(Ens.Id, value);
    }

    /// <summary>是否使用场景重力。</summary>
    public bool useGravity
    {
        get => RigidBodyBind.GetUseGravity(Ens.Id);
        set => RigidBodyBind.SetUseGravity(Ens.Id, value);
    }

    /// <summary>线性阻尼。</summary>
    public float linearDamping
    {
        get => RigidBodyBind.GetLinearDamping(Ens.Id);
        set => RigidBodyBind.SetLinearDamping(Ens.Id, value);
    }

    /// <summary>角阻尼。</summary>
    public float angularDamping
    {
        get => RigidBodyBind.GetAngularDamping(Ens.Id);
        set => RigidBodyBind.SetAngularDamping(Ens.Id, value);
    }

    /// <summary>世界空间线速度。</summary>
    public vector3 linearVelocity
    {
        get => RigidBodyBind.GetLinearVelocity(Ens.Id);
        set => RigidBodyBind.SetLinearVelocity(Ens.Id, value);
    }

    /// <summary>世界空间角速度。</summary>
    public vector3 angularVelocity
    {
        get => RigidBodyBind.GetAngularVelocity(Ens.Id);
        set => RigidBodyBind.SetAngularVelocity(Ens.Id, value);
    }

    /// <summary>是否启用连续碰撞检测。</summary>
    public bool continuousCollisionDetection
    {
        get => RigidBodyBind.GetContinuousCollisionDetection(Ens.Id);
        set => RigidBodyBind.SetContinuousCollisionDetection(Ens.Id, value);
    }

    /// <summary>刚体轴锁定位。</summary>
    public PhysicsLockFlags lockFlags
    {
        get => RigidBodyBind.GetLockFlags(Ens.Id);
        set => RigidBodyBind.SetLockFlags(Ens.Id, value);
    }
}

/// <summary>碰撞体组件包装。</summary>
public sealed class Collider : Component
{
    /// <summary>创建碰撞体组件包装。</summary>
    internal Collider(Ens ens, IntPtr pointer) : base(ens, pointer) {}

    //从原生指针获取 Collider 包装
    internal static Collider? FromNative(Ens ens, IntPtr pointer)
    {
        return Object.FromNative(pointer, value => new Collider(ens, value));
    }

    /// <summary>是否启用碰撞体。</summary>
    public bool enabled
    {
        get => ColliderBind.GetEnabled(Ens.Id);
        set => ColliderBind.SetEnabled(Ens.Id, value);
    }

    /// <summary>碰撞体几何类型。</summary>
    public ColliderShape shape
    {
        get => ColliderBind.GetShape(Ens.Id);
        set => ColliderBind.SetShape(Ens.Id, value);
    }

    /// <summary>是否作为触发器。</summary>
    public bool isTrigger
    {
        get => ColliderBind.GetIsTrigger(Ens.Id);
        set => ColliderBind.SetIsTrigger(Ens.Id, value);
    }

    /// <summary>相对实体原点的碰撞中心。</summary>
    public vector3 center
    {
        get => ColliderBind.GetCenter(Ens.Id);
        set => ColliderBind.SetCenter(Ens.Id, value);
    }

    /// <summary>盒体半尺寸。</summary>
    public vector3 halfExtents
    {
        get => ColliderBind.GetHalfExtents(Ens.Id);
        set => ColliderBind.SetHalfExtents(Ens.Id, value);
    }

    /// <summary>球体或胶囊体半径。</summary>
    public float radius
    {
        get => ColliderBind.GetRadius(Ens.Id);
        set => ColliderBind.SetRadius(Ens.Id, value);
    }

    /// <summary>胶囊体圆柱部分半高度。</summary>
    public float halfHeight
    {
        get => ColliderBind.GetHalfHeight(Ens.Id);
        set => ColliderBind.SetHalfHeight(Ens.Id, value);
    }

    /// <summary>凸包或三角网格资源。</summary>
    public Mesh? mesh
    {
        get => Mesh.FromNative(ColliderBind.GetMesh(Ens.Id));
        set => ColliderBind.SetMesh(Ens.Id, value?.NativePtr ?? IntPtr.Zero);
    }

    /// <summary>静摩擦系数。</summary>
    public float staticFriction
    {
        get => ColliderBind.GetStaticFriction(Ens.Id);
        set => ColliderBind.SetStaticFriction(Ens.Id, value);
    }

    /// <summary>动摩擦系数。</summary>
    public float dynamicFriction
    {
        get => ColliderBind.GetDynamicFriction(Ens.Id);
        set => ColliderBind.SetDynamicFriction(Ens.Id, value);
    }

    /// <summary>弹性系数。</summary>
    public float restitution
    {
        get => ColliderBind.GetRestitution(Ens.Id);
        set => ColliderBind.SetRestitution(Ens.Id, value);
    }

    /// <summary>碰撞层位。</summary>
    public uint collisionLayer
    {
        get => ColliderBind.GetCollisionLayer(Ens.Id);
        set => ColliderBind.SetCollisionLayer(Ens.Id, value);
    }

    /// <summary>允许碰撞的层掩码。</summary>
    public uint collisionMask
    {
        get => ColliderBind.GetCollisionMask(Ens.Id);
        set => ColliderBind.SetCollisionMask(Ens.Id, value);
    }
}

/// <summary>角色控制器组件包装。</summary>
public sealed class CharacterController : Component
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
        get => CharacterControllerBind.GetEnabled(Ens.Id);
        set => CharacterControllerBind.SetEnabled(Ens.Id, value);
    }

    /// <summary>角色控制器几何类型。</summary>
    public CharacterControllerShape shape
    {
        get => CharacterControllerBind.GetShape(Ens.Id);
        set => CharacterControllerBind.SetShape(Ens.Id, value);
    }

    /// <summary>胶囊控制器半径。</summary>
    public float radius
    {
        get => CharacterControllerBind.GetRadius(Ens.Id);
        set => CharacterControllerBind.SetRadius(Ens.Id, value);
    }

    /// <summary>胶囊控制器圆柱部分高度。</summary>
    public float height
    {
        get => CharacterControllerBind.GetHeight(Ens.Id);
        set => CharacterControllerBind.SetHeight(Ens.Id, value);
    }

    /// <summary>盒形控制器半尺寸。</summary>
    public vector3 halfExtents
    {
        get => CharacterControllerBind.GetHalfExtents(Ens.Id);
        set => CharacterControllerBind.SetHalfExtents(Ens.Id, value);
    }

    /// <summary>允许自动跨越的台阶高度。</summary>
    public float stepOffset
    {
        get => CharacterControllerBind.GetStepOffset(Ens.Id);
        set => CharacterControllerBind.SetStepOffset(Ens.Id, value);
    }

    /// <summary>控制器表面接触偏移。</summary>
    public float contactOffset
    {
        get => CharacterControllerBind.GetContactOffset(Ens.Id);
        set => CharacterControllerBind.SetContactOffset(Ens.Id, value);
    }

    /// <summary>可攀爬斜坡的法线阈值。</summary>
    public float slopeLimit
    {
        get => CharacterControllerBind.GetSlopeLimit(Ens.Id);
        set => CharacterControllerBind.SetSlopeLimit(Ens.Id, value);
    }

    /// <summary>忽略的最小移动距离。</summary>
    public float minMoveDistance
    {
        get => CharacterControllerBind.GetMinMoveDistance(Ens.Id);
        set => CharacterControllerBind.SetMinMoveDistance(Ens.Id, value);
    }

    /// <summary>控制器碰撞层位。</summary>
    public uint collisionLayer
    {
        get => CharacterControllerBind.GetCollisionLayer(Ens.Id);
        set => CharacterControllerBind.SetCollisionLayer(Ens.Id, value);
    }

    /// <summary>允许碰撞的层掩码。</summary>
    public uint collisionMask
    {
        get => CharacterControllerBind.GetCollisionMask(Ens.Id);
        set => CharacterControllerBind.SetCollisionMask(Ens.Id, value);
    }
}
