using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct NativeBindingsApi
{
    internal delegate* unmanaged[Cdecl]<uint> GetGeneration;
    internal delegate* unmanaged[Cdecl]<byte*, int, ulong, uint*, void***, NativeBindingStatus> ResolveType;
    internal delegate* unmanaged[Cdecl]<int, byte*> GetObjectTypeName;
    internal delegate* unmanaged[Cdecl]<int, IntPtr> GetObjectPointer;
    internal delegate* unmanaged[Cdecl]<int, EnsId> GetObjectEns;
    internal delegate* unmanaged[Cdecl]<uint, int> CreateObject;
    internal delegate* unmanaged[Cdecl]<EnsId, uint, int> AddComponent;
    internal delegate* unmanaged[Cdecl]<EnsId, uint, int*, int, int> GetComponents;
    internal delegate* unmanaged[Cdecl]<uint, byte*, int, int> LoadResource;
    internal delegate* unmanaged[Cdecl]<NativeBindingBuffer, void> ReleaseBuffer;
}

/// <summary>连接生成的原生函数表、类型工厂和包装身份。</summary>
public static unsafe class NativeBindingRuntime
{
    private sealed class Entry(Type type, string name, ulong signature, Func<Ens?, IntPtr, Object>? factory)
    {
        internal readonly Type Type = type;
        internal readonly string Name = name;
        internal readonly ulong Signature = signature;
        internal readonly Func<Ens?, IntPtr, Object>? Factory = factory;
        internal uint Generation;
        internal uint TypeId;
        internal void** Functions;
    }
    private static readonly Dictionary<Type, Entry> types = [];
    private static readonly Dictionary<string, Entry> names = new(StringComparer.Ordinal);
    private static NativeBindingsApi api;
    internal static uint Generation => api.GetGeneration == null ? 0 : api.GetGeneration();

    /// <summary>由生成模块注册自己的类型；不创建任何原生实例。</summary>
    public static void Register(Type type, string name, ulong signature, Func<Ens?, IntPtr, Object>? factory)
    {
        if (types.TryGetValue(type, out Entry? old))
        {
            if (old.Name != name || old.Signature != signature) throw new TypeLoadException($"Conflicting binding for {type}.");
            return;
        }
        if (names.TryGetValue(name, out Entry? named) && named.Signature != signature)
            throw new TypeLoadException($"Conflicting native binding signature for '{name}'.");
        Entry entry = new(type, name, signature, factory); types.Add(type, entry); names.TryAdd(name, entry);
    }

    /// <summary>为生成代码取得签名校验后的类型化函数指针。</summary>
    public static IntPtr GetFunction(Type type, int slot, Object? instance = null)
    {
        if (!ReferenceEquals(instance, null) && (!instance.IsAlive || instance.BindingGeneration != Generation))
            throw new ObjectDisposedException(instance.GetType().FullName, "Native binding instance was destroyed or its module was unloaded.");
        Entry entry = Resolve(type);
        return (IntPtr)entry.Functions[slot];
    }

    /// <summary>编码对象参数前验证包装仍属于当前原生 generation。</summary>
    public static int GetObjectId(Object? value)
    {
        if (ReferenceEquals(value, null)) return 0;
        if (!value.IsAlive || value.BindingGeneration != Generation)
            throw new ObjectDisposedException(value.GetType().FullName);
        return value.InstanceId;
    }

    /// <summary>把原生调用状态转为明确的托管异常。</summary>
    public static void Check(NativeBindingStatus status)
    {
        if (status == NativeBindingStatus.Ok) return;
        if (status == NativeBindingStatus.InvalidObject) throw new ObjectDisposedException("Native object");
        if (status == NativeBindingStatus.TypeMismatch) throw new TypeLoadException("Generated binding signature does not match the native module.");
        if (status == NativeBindingStatus.InvalidArgument) throw new ArgumentException("Native binding rejected an argument.");
        throw new InvalidOperationException("Native binding invocation failed.");
    }

    /// <summary>释放单次调用返回的原生快照。</summary>
    public static void Release(NativeBindingBuffer buffer)
    {
        if (api.ReleaseBuffer == null) throw new InvalidOperationException("Native binding API is disconnected.");
        api.ReleaseBuffer(buffer);
    }

    /// <summary>按实际原生类型包装对象，基类查询不会降级实例类型。</summary>
    public static T? Wrap<T>(int objectId) where T : Object => Wrap(objectId, typeof(T)) as T;

    internal static Object? Wrap(int objectId, Type? requestedType = null)
    {
        if (objectId == 0 || api.GetObjectPointer == null) return null;
        Object? cached = Object.FindCachedObject(objectId);
        if (cached != null && cached.BindingGeneration == Generation)
        {
            if (requestedType != null && !requestedType.IsInstanceOfType(cached))
                throw new InvalidOperationException("The native instance already has a wrapper from another managed type or load context.");
            return cached;
        }
        IntPtr pointer = api.GetObjectPointer(objectId);
        if (pointer == IntPtr.Zero) return null;
        string? name = Marshal.PtrToStringUTF8((IntPtr)api.GetObjectTypeName(objectId));
        if (name == "Script") return ScriptRuntime.GetOrCreateHost(pointer);
        if (name == null || !names.TryGetValue(name, out Entry? entry) || entry.Factory == null)
            throw new TypeLoadException($"No generated wrapper factory for native type '{name}'.");
        if (requestedType != null)
        {
            Entry? requested = types.Values.FirstOrDefault(candidate => candidate.Name == name
                && candidate.Type.Assembly == requestedType.Assembly && requestedType.IsAssignableFrom(candidate.Type));
            if (requested != null) entry = requested;
        }
        Resolve(entry.Type);
        EnsId ens = api.GetObjectEns(objectId);
        return entry.Factory(ens.IsNull ? null : Ens.FromId(ens), pointer);
    }

    /// <summary>区分托管脚本与生成的原生脚本包装。</summary>
    public static bool IsManagedScript(Type type)
    {
        if (type == typeof(Script) || !typeof(Script).IsAssignableFrom(type) || type.IsDefined(typeof(NativeBindingAttribute), false)) return false;
        for (Type? parent = type.BaseType; parent != null && parent != typeof(Script); parent = parent.BaseType)
            if (parent.IsDefined(typeof(NativeBindingAttribute), false))
                throw new InvalidOperationException($"Managed script '{type.FullName}' cannot derive from native wrapper '{parent.FullName}'.");
        return true;
    }

    internal static bool IsNativeType(Type type) => types.ContainsKey(type);
    internal static Object? Create(Type type)
    {
        if (type.IsAbstract || typeof(Component).IsAssignableFrom(type)) throw new ArgumentException("CreateInstance requires a concrete non-component Object type.");
        return Wrap(api.CreateObject(Resolve(type).TypeId), type);
    }
    internal static Component? AddComponent(EnsId ens, Type type) => Wrap(api.AddComponent(ens, Resolve(type).TypeId), type) as Component;
    internal static List<Component> GetComponents(EnsId ens, Type requestedType)
    {
        Entry entry = Resolve(types.ContainsKey(requestedType) ? requestedType : typeof(Component));
        int count = api.GetComponents(ens, entry.TypeId, null, 0);
        int[] ids = new int[count];
        fixed (int* pointer = ids)
        {
            int actual = api.GetComponents(ens, entry.TypeId, pointer, ids.Length);
            if (actual != count) throw new InvalidOperationException("Component collection changed while enumerating bindings.");
        }
        List<Component> result = [];
        foreach (int id in ids)
            if (Wrap(id, types.ContainsKey(requestedType) ? requestedType : null) is Component component && requestedType.IsInstanceOfType(component)) result.Add(component);
        return result;
    }
    internal static T? Load<T>(string key) where T : Object
    {
        byte[] bytes = Encoding.UTF8.GetBytes(key);
        uint typeId = Resolve(typeof(T)).TypeId;
        fixed (byte* pointer = bytes) return Wrap<T>(api.LoadResource(typeId, pointer, bytes.Length));
    }
    internal static void Initialize(NativeBindingsApi value)
    {
        api = value;
        foreach (Entry entry in types.Values) { entry.Generation = 0; entry.Functions = null; }
    }
    /// <summary>让运行中的游戏程序集提供基类查询所需的默认派生工厂。</summary>
    internal static void ActivateAssembly(Assembly assembly)
    {
        foreach (Entry entry in types.Values.Where(entry => entry.Type.Assembly == assembly))
            names[entry.Name] = entry;
    }

    /// <summary>卸载程序集前释放生成工厂及类型引用。</summary>
    public static void UnregisterAssembly(Assembly assembly)
    {
        Object.DisconnectAssembly(assembly);
        foreach (Entry entry in types.Values.Where(entry => entry.Type.Assembly == assembly).ToArray())
        {
            types.Remove(entry.Type);
            if (names.TryGetValue(entry.Name, out Entry? current) && ReferenceEquals(current, entry))
            {
                names.Remove(entry.Name);
                Entry? remaining = types.Values.LastOrDefault(candidate => candidate.Name == entry.Name);
                if (remaining != null) names.Add(entry.Name, remaining);
            }
        }
    }
    private static Entry Resolve(Type type)
    {
        if (api.ResolveType == null) throw new InvalidOperationException("Native binding API is disconnected.");
        if (!types.TryGetValue(type, out Entry? entry)) throw new TypeLoadException($"No generated binding for {type}.");
        uint generation = Generation;
        if (entry.Generation == generation) return entry;
        byte[] name = Encoding.UTF8.GetBytes(entry.Name);
        uint id; void** functions;
        fixed (byte* pointer = name) Check(api.ResolveType(pointer, name.Length, entry.Signature, &id, &functions));
        entry.TypeId = id; entry.Functions = functions; entry.Generation = generation; return entry;
    }
}
