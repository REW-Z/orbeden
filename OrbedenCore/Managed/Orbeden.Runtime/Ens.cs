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
    internal static Ens FromId(EnsId id)
    {
        if (id.IsNull) return Null;
        if (cache.TryGetValue(id, out Ens? value)) return value;

        value = new(id);
        cache[id] = value;
        return value;
    }

    /// <summary>判断 Ens 是否仍然有效。</summary>
    public bool IsValid => EnsBind.IsAlive(Id);

    /// <summary>Ens 名称。</summary>
    public string Name
    {
        get => EnsBind.GetName(Id);
        set => EnsBind.SetName(Id, value);
    }

    /// <summary>空间组件。</summary>
    public SpaceComponent Space => new(this);

    /// <summary>判断是否拥有 SpaceComponent。</summary>
    public bool HasSpaceComponent => EnsBind.HasSpaceComponent(Id);

    /// <summary>判断是否拥有 StaticMeshRenderer。</summary>
    public bool HasStaticMeshRenderer => EnsBind.HasStaticMeshRenderer(Id);

    /// <summary>获取组件包装。</summary>
    public T? GetComponent<T>() where T : Component
    {
        if (typeof(T) == typeof(SpaceComponent) && HasSpaceComponent)
        {
            return (T)(Component)new SpaceComponent(this);
        }

        if (typeof(T) == typeof(StaticMeshRenderer) && HasStaticMeshRenderer)
        {
            return (T)(Component)new StaticMeshRenderer(this);
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
