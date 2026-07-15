using System.Collections.Generic;

namespace Orbeden;

/// <summary>托管侧 Ens 代理，Native Ens 本体由 World 唯一持有。</summary>
public sealed class Ens : IEquatable<Ens>
{
    /// <summary>空 Ens。</summary>
    public static readonly Ens Null = new(EnsId.Null);

    private static readonly Dictionary<EnsId, Ens> cache = [];

    /// <summary>底层 EnsId。</summary>
    public EnsId Id { get; }

    private Ens(EnsId id)
    {
        Id = id;
    }

    /// <summary>通过 EnsId 获取托管代理。</summary>
    public static Ens FromId(EnsId id)
    {
        if (id.IsNull) return Null;
        if (cache.TryGetValue(id, out Ens? value)) return value;

        value = new(id);
        cache[id] = value;
        return value;
    }

    /// <summary>创建新的 Ens。</summary>
    public static Ens Create(string name = "")
    {
        return FromId(WorldBind.CreateEns(name));
    }

    /// <summary>使用稳定 ID 创建新的 Ens。</summary>
    public static Ens CreateWithStableId(string stableId, string name = "")
    {
        return FromId(WorldBind.CreateEnsWithStableId(stableId, name));
    }

    /// <summary>按稳定 ID 查找 Ens。</summary>
    public static Ens Find(string stableId)
    {
        return FromId(WorldBind.FindEns(stableId));
    }

    /// <summary>判断 Ens 是否仍然有效。</summary>
    public bool IsValid => EnsBind.IsAlive(Id);

    /// <summary>Ens 名称。</summary>
    public string Name
    {
        get => EnsBind.GetName(Id);
        set => EnsBind.SetName(Id, value);
    }

    /// <summary>销毁 Ens。</summary>
    public bool Destroy()
    {
        return WorldBind.DestroyEns(Id);
    }

    /// <summary>空间组件。</summary>
    public SpaceComponent Space => SpaceComponent.FromNative(this, EnsBind.GetSpaceComponent(Id))!;

    /// <summary>判断是否拥有 SpaceComponent。</summary>
    public bool HasSpaceComponent => EnsBind.HasSpaceComponent(Id);

    /// <summary>判断是否拥有 StaticMeshRenderer。</summary>
    public bool HasStaticMeshRenderer => EnsBind.HasStaticMeshRenderer(Id);

    /// <summary>判断是否拥有 RigidBody。</summary>
    public bool HasRigidBody => RigidBodyBind.HasComponent(Id);

    /// <summary>判断是否拥有 Collider。</summary>
    public bool HasCollider => ColliderBind.HasComponent(Id);

    /// <summary>判断是否拥有 CharacterController。</summary>
    public bool HasCharacterController => CharacterControllerBind.HasComponent(Id);

    /// <summary>添加静态网格渲染组件。</summary>
    public StaticMeshRenderer? AddStaticMeshRenderer()
    {
        return StaticMeshRenderer.FromNative(this, EnsBind.AddStaticMeshRenderer(Id));
    }

    /// <summary>添加刚体组件。</summary>
    public RigidBody? AddRigidBody()
    {
        return RigidBody.FromNative(this, RigidBodyBind.AddComponent(Id));
    }

    /// <summary>添加碰撞体组件。</summary>
    public Collider? AddCollider()
    {
        return Collider.FromNative(this, ColliderBind.AddComponent(Id));
    }

    /// <summary>添加角色控制器组件。</summary>
    public CharacterController? AddCharacterController()
    {
        return CharacterController.FromNative(this, CharacterControllerBind.AddComponent(Id));
    }

    /// <summary>获取组件包装。</summary>
    public T? GetComponent<T>() where T : Component
    {
        if (typeof(T) == typeof(SpaceComponent) && HasSpaceComponent)
        {
            return (T?)(Component?)SpaceComponent.FromNative(this, EnsBind.GetSpaceComponent(Id));
        }

        if (typeof(T) == typeof(StaticMeshRenderer) && HasStaticMeshRenderer)
        {
            return (T?)(Component?)StaticMeshRenderer.FromNative(this, EnsBind.GetStaticMeshRenderer(Id));
        }

        if (typeof(T) == typeof(RigidBody) && HasRigidBody)
        {
            return (T?)(Component?)RigidBody.FromNative(this, RigidBodyBind.GetComponent(Id));
        }

        if (typeof(T) == typeof(Collider) && HasCollider)
        {
            return (T?)(Component?)Collider.FromNative(this, ColliderBind.GetComponent(Id));
        }

        if (typeof(T) == typeof(CharacterController) && HasCharacterController)
        {
            return (T?)(Component?)CharacterController.FromNative(this, CharacterControllerBind.GetComponent(Id));
        }

        return null;
    }

    /// <summary>尝试获取组件包装。</summary>
    public bool TryGetComponent<T>(out T? component) where T : Component
    {
        component = GetComponent<T>();
        return component != null;
    }

    /// <summary>判断两个 Ens 是否相同。</summary>
    public bool Equals(Ens? other)
    {
        return other != null && Id.Equals(other.Id);
    }

    /// <summary>判断两个 Ens 是否相同。</summary>
    public override bool Equals(object? obj)
    {
        return obj is Ens other && Equals(other);
    }

    /// <summary>获取哈希值。</summary>
    public override int GetHashCode()
    {
        return Id.GetHashCode();
    }
}
