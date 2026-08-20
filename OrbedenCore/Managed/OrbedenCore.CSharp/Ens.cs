using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

/// <summary>托管侧 Ens 代理，Native Ens 本体由 World 唯一持有。</summary>
public sealed partial class Ens : IEquatable<Ens>
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
        return FromId(CreateEns(name));
    }

    /// <summary>使用稳定 ID 创建新的 Ens。</summary>
    public static Ens CreateWithStableId(string stableId, string name = "")
    {
        return FromId(CreateEnsWithStableId(stableId, name));
    }

    /// <summary>按稳定 ID 查找 Ens。</summary>
    public static Ens Find(string stableId)
    {
        return FromId(FindEns(stableId));
    }

    /// <summary>判断 Ens 是否仍然有效。</summary>
    public bool IsValid => IsAlive(Id);

    /// <summary>Ens 自身设置的激活状态。</summary>
    public bool LocalActive
    {
        get => GetLocalActive(Id);
        set => SetLocalActive(Id, value);
    }

    /// <summary>Ens 经父子层级计算后的实际激活状态。</summary>
    public bool WorldActive => GetWorldActive(Id);

    /// <summary>Ens 名称。</summary>
    public string Name
    {
        get => GetName(Id);
        set => SetName(Id, value);
    }

    /// <summary>销毁 Ens。</summary>
    public bool Destroy()
    {
        return DestroyEns(Id);
    }

    /// <summary>变换组件。</summary>
    public TransformComponent Transform => TransformComponent.FromNative(this, GetTransformComponent(Id))!;

    /// <summary>判断是否拥有 TransformComponent。</summary>
    public bool HasTransformComponent => NativeHasTransformComponent(Id);

    /// <summary>判断是否拥有 StaticMeshRenderer。</summary>
    public bool HasStaticMeshRenderer => NativeHasStaticMeshRenderer(Id);

    /// <summary>判断是否拥有 RigidBody。</summary>
    public bool HasRigidBody => HasComponent<RigidBody>();

    /// <summary>判断是否拥有任意 Collider。</summary>
    public bool HasCollider => GetComponents<Collider>().Length != 0;

    /// <summary>判断是否拥有 CharacterController。</summary>
    public bool HasCharacterController => HasComponent<CharacterController>();

    /// <summary>添加静态网格渲染组件。</summary>
    public StaticMeshRenderer? AddStaticMeshRenderer()
    {
        return AddComponent<StaticMeshRenderer>();
    }

    /// <summary>添加刚体组件。</summary>
    public RigidBody? AddRigidBody()
    {
        return AddComponent<RigidBody>();
    }

    /// <summary>添加盒形碰撞体组件。</summary>
    public BoxCollider? AddBoxCollider()
    {
        return AddComponent<BoxCollider>();
    }

    /// <summary>添加球形碰撞体组件。</summary>
    public SphereCollider? AddSphereCollider()
    {
        return AddComponent<SphereCollider>();
    }

    /// <summary>添加胶囊碰撞体组件。</summary>
    public CapsuleCollider? AddCapsuleCollider()
    {
        return AddComponent<CapsuleCollider>();
    }

    /// <summary>添加凸包网格碰撞体组件。</summary>
    public ConvexMeshCollider? AddConvexMeshCollider()
    {
        return AddComponent<ConvexMeshCollider>();
    }

    /// <summary>添加三角网格碰撞体组件。</summary>
    public TriangleMeshCollider? AddTriangleMeshCollider()
    {
        return AddComponent<TriangleMeshCollider>();
    }

    /// <summary>添加角色控制器组件。</summary>
    public CharacterController? AddCharacterController()
    {
        return AddComponent<CharacterController>();
    }

    /// <summary>添加组件，并自动补齐其依赖。</summary>
    public T? AddComponent<T>() where T : Component
    {
        Component? component = AddComponent(typeof(T));
        return component as T;
    }

    /// <summary>获取最先挂载的指定类型组件。</summary>
    public T? GetComponent<T>() where T : Component
    {
        T[] components = GetComponents<T>();
        return components.Length != 0 ? components[0] : null;
    }

    /// <summary>获取指定类型的所有组件，顺序与挂载顺序一致。</summary>
    public T[] GetComponents<T>() where T : Component
    {
        List<T> result = [];
        foreach (Component component in GetNativeComponents(typeof(T)))
        {
            if (component is T value) result.Add(value);
        }
        return [.. result];
    }

    /// <summary>判断是否拥有指定类型组件。</summary>
    public bool HasComponent<T>() where T : Component => GetComponent<T>() != null;

    /// <summary>尝试获取组件包装。</summary>
    public bool TryGetComponent<T>(out T? component) where T : Component
    {
        component = GetComponent<T>();
        return component != null;
    }

    //收集原生组件包装
    private List<Component> GetNativeComponents(Type requestedType)
    {
        List<Component> result = [];
        if (requestedType.IsAssignableFrom(typeof(TransformComponent)) && HasTransformComponent)
        {
            TransformComponent? transform = TransformComponent.FromNative(this, GetTransformComponent(Id));
            if (transform != null) result.Add(transform);
        }

        if (requestedType.IsAssignableFrom(typeof(StaticMeshRenderer)) && HasStaticMeshRenderer)
        {
            StaticMeshRenderer? renderer = StaticMeshRenderer.FromNative(this, GetStaticMeshRenderer(Id));
            if (renderer != null) result.Add(renderer);
        }

        if (requestedType.IsAssignableFrom(typeof(RigidBody)) && RigidBody.HasComponent(Id))
        {
            RigidBody? body = RigidBody.FromNative(this, RigidBody.GetComponent(Id));
            if (body != null) result.Add(body);
        }

        foreach (Collider collider in Collider.GetComponents(this))
        {
            if (requestedType.IsAssignableFrom(collider.GetType())) result.Add(collider);
        }

        if (requestedType.IsAssignableFrom(typeof(CharacterController)) && CharacterController.HasComponent(Id))
        {
            CharacterController? controller = CharacterController.FromNative(this, CharacterController.GetComponent(Id));
            if (controller != null) result.Add(controller);
        }

        foreach (ScriptBehaviour script in ScriptRuntimeRegistry.GetScripts(Id))
        {
            if (requestedType.IsAssignableFrom(script.GetType())) result.Add(script);
        }

        return result;
    }

    //验证组件依赖图并生成创建顺序
    private static void BuildComponentAddOrder(Type componentType, HashSet<Type> visiting, HashSet<Type> visited, List<Type> order)
    {
        if (!typeof(Component).IsAssignableFrom(componentType) || componentType.IsAbstract || !HasNativeFactory(componentType))
        {
            throw new InvalidOperationException($"无法通过 Ens 创建组件 {componentType.FullName}。");
        }

        if (!visiting.Add(componentType))
        {
            throw new InvalidOperationException($"组件依赖存在循环：{componentType.FullName}。");
        }

        foreach (DependsOnComponentAttribute dependency in componentType.GetCustomAttributes<DependsOnComponentAttribute>(true))
        {
            foreach (Type requiredType in dependency.ComponentTypes)
            {
                if (requiredType == null) throw new InvalidOperationException($"组件 {componentType.FullName} 包含空依赖。");
                BuildComponentAddOrder(requiredType, visiting, visited, order);
            }
        }

        visiting.Remove(componentType);
        if (visited.Add(componentType)) order.Add(componentType);
    }

    //按规则添加原生组件
    private Component? AddComponent(Type componentType)
    {
        List<Type> order = [];
        BuildComponentAddOrder(componentType, [], [], order);

        Component? requested = null;
        foreach (Type type in order)
        {
            Component? existing = GetFirstNativeComponent(type);
            bool isRequestedType = type == componentType;
            if ((!isRequestedType || type.GetCustomAttribute<UniqueComponentAttribute>(true) != null) && existing != null)
            {
                if (isRequestedType) requested = existing;
                continue;
            }

            Component? created = CreateNativeComponent(type);
            if (created == null) throw new InvalidOperationException($"原生组件 {type.FullName} 创建失败。");
            if (isRequestedType) requested = created;
        }

        return requested ?? GetFirstNativeComponent(componentType);
    }

    //获取指定原生组件的首个实例
    private Component? GetFirstNativeComponent(Type componentType)
    {
        List<Component> components = GetNativeComponents(componentType);
        return components.Count != 0 ? components[0] : null;
    }

    //判断类型是否具有原生工厂
    private static bool HasNativeFactory(Type componentType)
    {
        return componentType == typeof(TransformComponent) ||
               componentType == typeof(StaticMeshRenderer) ||
               componentType == typeof(RigidBody) ||
               componentType == typeof(BoxCollider) ||
               componentType == typeof(SphereCollider) ||
               componentType == typeof(CapsuleCollider) ||
               componentType == typeof(ConvexMeshCollider) ||
               componentType == typeof(TriangleMeshCollider) ||
               componentType == typeof(CharacterController);
    }

    //创建一个原生组件实例
    private Component? CreateNativeComponent(Type componentType)
    {
        if (componentType == typeof(TransformComponent)) return Transform;
        if (componentType == typeof(StaticMeshRenderer)) return StaticMeshRenderer.FromNative(this, AddStaticMeshRenderer(Id));
        if (componentType == typeof(RigidBody)) return RigidBody.FromNative(this, RigidBody.AddComponent(Id));
        if (componentType == typeof(BoxCollider)) return Collider.AddBoxCollider(this);
        if (componentType == typeof(SphereCollider)) return Collider.AddSphereCollider(this);
        if (componentType == typeof(CapsuleCollider)) return Collider.AddCapsuleCollider(this);
        if (componentType == typeof(ConvexMeshCollider)) return Collider.AddConvexMeshCollider(this);
        if (componentType == typeof(TriangleMeshCollider)) return Collider.AddTriangleMeshCollider(this);
        if (componentType == typeof(CharacterController)) return CharacterController.FromNative(this, CharacterController.AddComponent(Id));
        return null;
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

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EnsBindApi
{
    public delegate* unmanaged[Cdecl]<EnsId, byte> IsAlive;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetLocalActive;
    public delegate* unmanaged[Cdecl]<EnsId, byte> GetWorldActive;
    public delegate* unmanaged[Cdecl]<EnsId, byte, void> SetLocalActive;
    public delegate* unmanaged[Cdecl]<EnsId, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<EnsId, byte*, int, void> SetName;
    public delegate* unmanaged[Cdecl]<EnsId, byte> HasTransformComponent;
    public delegate* unmanaged[Cdecl]<EnsId, byte> HasStaticMeshRenderer;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> AddStaticMeshRenderer;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> GetTransformComponent;
    public delegate* unmanaged[Cdecl]<EnsId, IntPtr> GetStaticMeshRenderer;
}
#pragma warning restore CS0649

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct WorldBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, EnsId> CreateEns;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, EnsId> CreateEnsWithStableId;
    public delegate* unmanaged[Cdecl]<byte*, int, EnsId> FindEns;
    public delegate* unmanaged[Cdecl]<EnsId, byte> DestroyEns;
}
#pragma warning restore CS0649

public sealed unsafe partial class Ens
{
    private static EnsBindApi ensApi;
    private static bool ensApiInitialized;

    //保存 C++ 传入的 Ens 函数表
    internal static void InitializeEnsNativeApi(EnsBindApi value)
    {
        ensApi = value;
        ensApiInitialized = ensApi.IsAlive != null;
    }

    //判断 Ens 是否有效
    internal static bool IsAlive(EnsId ens)
    {
        return ensApiInitialized && ensApi.IsAlive != null && ensApi.IsAlive(ens) != 0;
    }

    //读取 Ens 的 localActive
    internal static bool GetLocalActive(EnsId ens)
    {
        return ensApiInitialized && ensApi.GetLocalActive != null && ensApi.GetLocalActive(ens) != 0;
    }

    //读取 Ens 的 worldActive
    internal static bool GetWorldActive(EnsId ens)
    {
        return ensApiInitialized && ensApi.GetWorldActive != null && ensApi.GetWorldActive(ens) != 0;
    }

    //设置 Ens 的 localActive
    internal static void SetLocalActive(EnsId ens, bool active)
    {
        if (ensApiInitialized && ensApi.SetLocalActive != null) ensApi.SetLocalActive(ens, active ? (byte)1 : (byte)0);
    }

    //读取 Ens 名称
    internal static string GetName(EnsId ens)
    {
        if (!ensApiInitialized || ensApi.GetName == null) return string.Empty;

        int requiredBytes = ensApi.GetName(ens, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> bytes = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* pointer = bytes)
        {
            int actualBytes = ensApi.GetName(ens, pointer, requiredBytes);
            int length = Math.Clamp(actualBytes, 0, requiredBytes);
            return Encoding.UTF8.GetString(bytes[..length]);
        }
    }

    //写入 Ens 名称
    internal static void SetName(EnsId ens, string? name)
    {
        if (!ensApiInitialized || ensApi.SetName == null) return;

        string value = name ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        if (byteCount <= 0)
        {
            ensApi.SetName(ens, null, 0);
            return;
        }

        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[byteCount] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);
        fixed (byte* pointer = bytes)
        {
            ensApi.SetName(ens, pointer, byteCount);
        }
    }

    //判断 Ens 是否拥有 TransformComponent
    internal static bool NativeHasTransformComponent(EnsId ens)
    {
        return ensApiInitialized && ensApi.HasTransformComponent != null && ensApi.HasTransformComponent(ens) != 0;
    }

    //判断 Ens 是否拥有 StaticMeshRenderer
    internal static bool NativeHasStaticMeshRenderer(EnsId ens)
    {
        return ensApiInitialized && ensApi.HasStaticMeshRenderer != null && ensApi.HasStaticMeshRenderer(ens) != 0;
    }

    //添加 StaticMeshRenderer
    internal static IntPtr AddStaticMeshRenderer(EnsId ens)
    {
        return ensApiInitialized && ensApi.AddStaticMeshRenderer != null ? ensApi.AddStaticMeshRenderer(ens) : IntPtr.Zero;
    }

    //获取 TransformComponent 指针
    internal static IntPtr GetTransformComponent(EnsId ens)
    {
        return ensApiInitialized && ensApi.GetTransformComponent != null ? ensApi.GetTransformComponent(ens) : IntPtr.Zero;
    }

    //获取 StaticMeshRenderer 指针
    internal static IntPtr GetStaticMeshRenderer(EnsId ens)
    {
        return ensApiInitialized && ensApi.GetStaticMeshRenderer != null ? ensApi.GetStaticMeshRenderer(ens) : IntPtr.Zero;
    }
}

public sealed unsafe partial class Ens
{
    private static WorldBindApi worldApi;
    private static bool worldApiInitialized;

    //保存 C++ 传入的 World 函数表
    internal static void InitializeWorldNativeApi(WorldBindApi value)
    {
        worldApi = value;
        worldApiInitialized = worldApi.CreateEns != null;
    }

    //创建 Ens
    internal static EnsId CreateEns(string? name)
    {
        if (!worldApiInitialized || worldApi.CreateEns == null) return EnsId.Null;

        string value = name ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            return worldApi.CreateEns(pointer, byteCount);
        }
    }

    //使用稳定 ID 创建 Ens
    internal static EnsId CreateEnsWithStableId(string? stableId, string? name)
    {
        if (!worldApiInitialized || worldApi.CreateEnsWithStableId == null) return EnsId.Null;

        string stableValue = stableId ?? string.Empty;
        string nameValue = name ?? string.Empty;
        int stableBytesCount = Encoding.UTF8.GetByteCount(stableValue);
        int nameBytesCount = Encoding.UTF8.GetByteCount(nameValue);
        Span<byte> stableBytes = stableBytesCount <= 1024 ? stackalloc byte[Math.Max(stableBytesCount, 1)] : new byte[stableBytesCount];
        Span<byte> nameBytes = nameBytesCount <= 1024 ? stackalloc byte[Math.Max(nameBytesCount, 1)] : new byte[nameBytesCount];
        Encoding.UTF8.GetBytes(stableValue.AsSpan(), stableBytes);
        Encoding.UTF8.GetBytes(nameValue.AsSpan(), nameBytes);

        fixed (byte* stablePointer = stableBytes)
        fixed (byte* namePointer = nameBytes)
        {
            return worldApi.CreateEnsWithStableId(stablePointer, stableBytesCount, namePointer, nameBytesCount);
        }
    }

    //按稳定 ID 查找 Ens
    internal static EnsId FindEns(string? stableId)
    {
        if (!worldApiInitialized || worldApi.FindEns == null) return EnsId.Null;

        string value = stableId ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            return worldApi.FindEns(pointer, byteCount);
        }
    }

    //销毁 Ens
    internal static bool DestroyEns(EnsId ens)
    {
        return worldApiInitialized && worldApi.DestroyEns != null && worldApi.DestroyEns(ens) != 0;
    }
}
