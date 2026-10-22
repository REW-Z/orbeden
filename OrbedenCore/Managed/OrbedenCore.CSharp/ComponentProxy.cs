using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

/// <summary>脚本互操作统一状态。</summary>
public enum InteropStatus : uint
{
    Ok,
    NotFound,
    StaleHandle,
    InvalidArgument,
    TypeMismatch,
    AmbiguousMethod,
    UnsupportedType,
    WrongThread,
    ReentrancyLimit,
    InvocationFailed,
}

/// <summary>脚本互操作支持的值类型。</summary>
public enum InteropValueKind : uint
{
    Empty,
    Bool,
    Int32,
    UInt32,
    UInt64,
    Float32,
    String,
    StringId,
    Vector3,
    Color,
    Quaternion,
    EnsId,
    Object,
    //容器条目：值是元素个数，元素本身各自成条（与 Reflection::ValueKind 逐值对应）
    Array,
    Vector2,
}

internal enum ComponentDomain : uint
{
    None,
    Native,
    Managed,
}

internal enum InteropMemberKind : uint
{
    None,
    Field,
    Method,
    DirectMethod,
    DirectField,
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal readonly struct ComponentHandle
{
    public readonly ComponentDomain Domain;
    public readonly uint Generation;
    public readonly ulong Slot;

    public ComponentHandle(ComponentDomain domain, uint generation, ulong slot)
    {
        Domain = domain;
        Generation = generation;
        Slot = slot;
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal readonly struct MemberHandle
{
    public readonly ComponentDomain Domain;
    public readonly InteropMemberKind Kind;
    public readonly ulong Slot;
    public readonly uint Generation;
    public readonly uint Reserved;

    public MemberHandle(ComponentDomain domain, InteropMemberKind kind, ulong slot, uint generation)
    {
        Domain = domain;
        Kind = kind;
        Slot = slot;
        Generation = generation;
        Reserved = 0;
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct InteropValueAbi
{
    public uint Kind;
    public uint Reserved;
    public fixed byte Payload[16];
}

/// <summary>跨 C++/C# 边界复制的受限值。</summary>
public readonly unsafe struct InteropValue : IEquatable<InteropValue>
{
    internal readonly InteropValueAbi Abi;
    private readonly string? text;

    public InteropValueKind Kind => (InteropValueKind)Abi.Kind;

    /// <summary>按需取得对象表示；数值仅在访问此属性时装箱。</summary>
    public object? Value => Kind switch
    {
        InteropValueKind.Bool => Read<bool>(),
        InteropValueKind.Int32 or InteropValueKind.Object or InteropValueKind.Array => Read<int>(),
        InteropValueKind.UInt32 => Read<uint>(),
        InteropValueKind.UInt64 => Read<ulong>(),
        InteropValueKind.Float32 => Read<float>(),
        InteropValueKind.Vector2 => Read<vector2>(),
        InteropValueKind.Vector3 => Read<vector3>(),
        InteropValueKind.Color => Read<color>(),
        InteropValueKind.Quaternion => Read<quaternion>(),
        InteropValueKind.EnsId => Read<EnsId>(),
        InteropValueKind.String or InteropValueKind.StringId => text,
        _ => null,
    };

    internal InteropValue(InteropValueAbi abi, string? value = null)
    {
        Abi = abi;
        text = value;
    }

    //内联保存受支持的值类型
    private static InteropValue Scalar<T>(InteropValueKind kind, T value) where T : unmanaged
    {
        InteropValueAbi abi = default;
        abi.Kind = (uint)kind;
        Unsafe.WriteUnaligned(abi.Payload, value);
        return new(abi);
    }

    //读取内联数值
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private T Read<T>()
    {
        InteropValueAbi abi = Abi;
        return Unsafe.ReadUnaligned<T>(abi.Payload);
    }

    public static InteropValue Empty => default;
    public static InteropValue From(bool value) => Scalar(InteropValueKind.Bool, value);
    public static InteropValue From(int value) => Scalar(InteropValueKind.Int32, value);
    public static InteropValue From(uint value) => Scalar(InteropValueKind.UInt32, value);
    public static InteropValue From(ulong value) => Scalar(InteropValueKind.UInt64, value);
    public static InteropValue From(float value) => Scalar(InteropValueKind.Float32, value);
    public static InteropValue From(string? value) => new(new InteropValueAbi { Kind = (uint)InteropValueKind.String }, value ?? string.Empty);
    public static InteropValue FromStringId(string? value) => new(new InteropValueAbi { Kind = (uint)InteropValueKind.StringId }, value ?? string.Empty);
    public static InteropValue From(vector2 value) => Scalar(InteropValueKind.Vector2, value);
    public static InteropValue From(vector3 value) => Scalar(InteropValueKind.Vector3, value);
    public static InteropValue From(color value) => Scalar(InteropValueKind.Color, value);
    public static InteropValue From(quaternion value) => Scalar(InteropValueKind.Quaternion, value);
    public static InteropValue From(EnsId value) => Scalar(InteropValueKind.EnsId, value);
    public static InteropValue FromObject(Object? value) => FromObjectId(value?.InstanceId ?? 0);
    public static InteropValue FromObjectId(int value) => Scalar(InteropValueKind.Object, value);
    public static InteropValue FromArray(int count) => Scalar(InteropValueKind.Array, count);

    /// <summary>按精确类型读取数值；值类型读取不装箱。</summary>
    public bool TryGet<T>(out T value)
    {
        bool scalar = typeof(T) == typeof(bool) && Kind == InteropValueKind.Bool
            || typeof(T) == typeof(int) && Kind is InteropValueKind.Int32 or InteropValueKind.Object or InteropValueKind.Array
            || typeof(T) == typeof(uint) && Kind == InteropValueKind.UInt32
            || typeof(T) == typeof(ulong) && Kind == InteropValueKind.UInt64
            || typeof(T) == typeof(float) && Kind == InteropValueKind.Float32
            || typeof(T) == typeof(vector2) && Kind == InteropValueKind.Vector2
            || typeof(T) == typeof(vector3) && Kind == InteropValueKind.Vector3
            || typeof(T) == typeof(color) && Kind == InteropValueKind.Color
            || typeof(T) == typeof(quaternion) && Kind == InteropValueKind.Quaternion
            || typeof(T) == typeof(EnsId) && Kind == InteropValueKind.EnsId;
        if (scalar)
        {
            value = Read<T>();
            return true;
        }
        if (typeof(T) == typeof(string) && Kind is InteropValueKind.String or InteropValueKind.StringId)
        {
            string current = text!;
            value = Unsafe.As<string, T>(ref current);
            return true;
        }
        //对象类型读取保留显式装箱语义
        if (!typeof(T).IsValueType && Value is T boxed)
        {
            value = boxed;
            return true;
        }
        value = default!;
        return false;
    }

    //按浮点数值比较向量，保留 NaN 和正负零的相等语义
    private bool FloatComponentsEqual(InteropValue other, int count)
    {
        InteropValueAbi left = Abi;
        InteropValueAbi right = other.Abi;
        for (int i = 0; i < count; i++)
            if (!Unsafe.ReadUnaligned<float>(left.Payload + i * 4).Equals(Unsafe.ReadUnaligned<float>(right.Payload + i * 4))) return false;
        return true;
    }

    /// <summary>比较内联值，不为数值创建对象表示。</summary>
    public bool Equals(InteropValue other)
    {
        if (Kind != other.Kind) return false;
        return Kind switch
        {
            InteropValueKind.Empty => true,
            InteropValueKind.Bool => Read<bool>() == other.Read<bool>(),
            InteropValueKind.Int32 or InteropValueKind.Object or InteropValueKind.Array => Read<int>() == other.Read<int>(),
            InteropValueKind.UInt32 => Read<uint>() == other.Read<uint>(),
            InteropValueKind.UInt64 or InteropValueKind.EnsId => Read<ulong>() == other.Read<ulong>(),
            InteropValueKind.Float32 => Read<float>().Equals(other.Read<float>()),
            InteropValueKind.Vector2 => FloatComponentsEqual(other, 2),
            InteropValueKind.Vector3 => FloatComponentsEqual(other, 3),
            InteropValueKind.Color or InteropValueKind.Quaternion => FloatComponentsEqual(other, 4),
            InteropValueKind.String or InteropValueKind.StringId => string.Equals(text, other.text, StringComparison.Ordinal),
            _ => false,
        };
    }

    public override bool Equals(object? obj) => obj is InteropValue other && Equals(other);

    /// <summary>计算内联值的哈希，不为数值创建对象表示。</summary>
    public override int GetHashCode()
    {
        int count = Kind switch
        {
            InteropValueKind.Vector2 => 2,
            InteropValueKind.Vector3 => 3,
            InteropValueKind.Color or InteropValueKind.Quaternion => 4,
            _ => 0,
        };
        if (count > 0)
        {
            HashCode hash = new();
            hash.Add(Kind);
            InteropValueAbi abi = Abi;
            for (int i = 0; i < count; i++) hash.Add(Unsafe.ReadUnaligned<float>(abi.Payload + i * 4));
            return hash.ToHashCode();
        }
        return Kind switch
        {
            InteropValueKind.Bool => HashCode.Combine(Kind, Read<bool>()),
            InteropValueKind.Int32 or InteropValueKind.Object or InteropValueKind.Array => HashCode.Combine(Kind, Read<int>()),
            InteropValueKind.UInt32 => HashCode.Combine(Kind, Read<uint>()),
            InteropValueKind.UInt64 or InteropValueKind.EnsId => HashCode.Combine(Kind, Read<ulong>()),
            InteropValueKind.Float32 => HashCode.Combine(Kind, Read<float>()),
            InteropValueKind.String or InteropValueKind.StringId => HashCode.Combine(Kind, text),
            _ => HashCode.Combine(Kind, 0),
        };
    }
    public override string ToString() => Value?.ToString() ?? string.Empty;
}

/// <summary>绑定到一个组件实例的预解析方法，可重复调用而不再按名称查找。</summary>
public readonly struct ComponentMethod
{
    private readonly ComponentHandle component;
    private readonly MemberHandle method;

    internal ComponentMethod(ComponentHandle componentHandle, MemberHandle methodHandle)
    {
        component = componentHandle;
        method = methodHandle;
    }

    /// <summary>判断所属组件是否仍然有效；方法会在调用时继续校验代次。</summary>
    public bool IsValid => ScriptInteropDispatch.IsValid(component) == InteropStatus.Ok;

    /// <summary>使用已解析的方法句柄同步调用。</summary>
    public InteropStatus Invoke(ReadOnlySpan<InteropValue> arguments, out InteropValue result)
    {
        return ScriptInteropDispatch.Invoke(component, method, arguments, out result);
    }

    /// <summary>使用已解析的方法句柄同步调用。</summary>
    public InteropStatus Invoke(out InteropValue result, params ReadOnlySpan<InteropValue> arguments)
    {
        return Invoke(arguments, out result);
    }
}

/// <summary>一个可绑定方法的签名：名字、返回种类与参数种类。</summary>
public sealed class BindableMethod
{
    /// <summary>方法名。</summary>
    public string Name { get; }

    /// <summary>返回种类；无返回值为 Empty。</summary>
    public InteropValueKind ReturnKind { get; }

    /// <summary>参数种类，按声明顺序。</summary>
    public IReadOnlyList<InteropValueKind> ParameterKinds { get; }

    internal BindableMethod(string name, InteropValueKind returnKind, InteropValueKind[] parameterKinds)
    {
        Name = name;
        ReturnKind = returnKind;
        ParameterKinds = parameterKinds;
    }
}

/// <summary>预解析组件字段和方法的统一代理。</summary>
public sealed partial class ComponentProxy
{
    private readonly ComponentHandle handle;
    private readonly Dictionary<string, MemberHandle> fieldCache = new(StringComparer.Ordinal);
    private readonly Dictionary<string, List<(InteropValueKind[] Kinds, MemberHandle Member)>> methodCache = new(StringComparer.Ordinal);

    internal ComponentProxy(ComponentHandle value)
    {
        handle = value;
    }

    /// <summary>
    /// 取托管组件包装的代理。原生组件包装没有运行期句柄，返回空——
    /// 那种情况用 <see cref="Ens.GetNativeComponent"/> 按类型名取。
    /// </summary>
    public static ComponentProxy? FromComponent(Component? component)
    {
        if (component is not Script script) return null;
        return ScriptRuntimeRegistry.TryGetHandle(script, out ComponentHandle found) ? new ComponentProxy(found) : null;
    }

    /// <summary>
    /// 列出组件上可绑定的方法签名。元数据在程序集加载时按类型建一次并缓存，
    /// 枚举顺序按名字与参数个数排定，界面上的下拉因此不会每帧跳动。
    /// </summary>
    public static IReadOnlyList<BindableMethod> DescribeMethods(Component? component)
    {
        if (component == null) return [];

        List<BindableMethod> listed = [];
        foreach (List<ManagedMethodMetadata> overloads in ManagedTypeMetadataCache.Get(component.GetType()).Methods.Values)
        {
            foreach (ManagedMethodMetadata overload in overloads)
                listed.Add(new BindableMethod(overload.Method.Name, overload.ReturnKind, overload.ParameterKinds));
        }
        listed.Sort(static (left, right) =>
        {
            int byName = string.CompareOrdinal(left.Name, right.Name);
            return byName != 0 ? byName : left.ParameterKinds.Count.CompareTo(right.ParameterKinds.Count);
        });
        return listed;
    }

    public bool IsValid => ScriptInteropDispatch.IsValid(handle) == InteropStatus.Ok;

    public InteropStatus ResolveField(string name)
    {
        return ResolveFieldHandle(name, out _);
    }

    public InteropStatus ResolveMethod(string name, params ReadOnlySpan<InteropValueKind> parameterKinds)
    {
        return ResolveMethodHandle(name, parameterKinds, out _);
    }

    /// <summary>解析一次方法名和参数签名，返回可重复调用的方法对象。</summary>
    public InteropStatus TryResolveMethod(string name, ReadOnlySpan<InteropValueKind> parameterKinds, out ComponentMethod method)
    {
        InteropStatus status = ResolveMethodHandle(name, parameterKinds, out MemberHandle member);
        method = status == InteropStatus.Ok ? new ComponentMethod(handle, member) : default;
        return status;
    }

    /// <summary>解析一次方法名和参数签名，返回可重复调用的方法对象。</summary>
    public InteropStatus TryResolveMethod(string name, out ComponentMethod method, params ReadOnlySpan<InteropValueKind> parameterKinds)
    {
        return TryResolveMethod(name, parameterKinds, out method);
    }

    public InteropStatus TryGetField(string name, out InteropValue value)
    {
        value = default;
        InteropStatus status = ResolveFieldHandle(name, out MemberHandle member);
        return status == InteropStatus.Ok ? ScriptInteropDispatch.GetField(handle, member, out value) : status;
    }

    public InteropStatus SetField(string name, InteropValue value)
    {
        InteropStatus status = ResolveFieldHandle(name, out MemberHandle member);
        return status == InteropStatus.Ok ? ScriptInteropDispatch.SetField(handle, member, value) : status;
    }

    public InteropStatus Invoke(string name, ReadOnlySpan<InteropValue> arguments, out InteropValue result)
    {
        result = default;
        Span<InteropValueKind> kinds = arguments.Length <= 16 ? stackalloc InteropValueKind[arguments.Length] : new InteropValueKind[arguments.Length];
        for (int index = 0; index < arguments.Length; ++index) kinds[index] = arguments[index].Kind;
        InteropStatus status = ResolveMethodHandle(name, kinds, out MemberHandle member);
        return status == InteropStatus.Ok ? ScriptInteropDispatch.Invoke(handle, member, arguments, out result) : status;
    }

    public InteropStatus Invoke(string name, out InteropValue result, params ReadOnlySpan<InteropValue> arguments)
    {
        return Invoke(name, arguments, out result);
    }

    private InteropStatus ResolveFieldHandle(string name, out MemberHandle member)
    {
        if (string.IsNullOrEmpty(name))
        {
            member = default;
            return InteropStatus.InvalidArgument;
        }
        if (fieldCache.TryGetValue(name, out member)) return InteropStatus.Ok;

        InteropStatus status = ScriptInteropDispatch.ResolveField(handle, name, out member);
        if (status == InteropStatus.Ok) fieldCache.Add(name, member);
        return status;
    }

    private InteropStatus ResolveMethodHandle(string name, ReadOnlySpan<InteropValueKind> kinds, out MemberHandle member)
    {
        member = default;
        if (string.IsNullOrEmpty(name)) return InteropStatus.InvalidArgument;
        if (methodCache.TryGetValue(name, out var overloads))
        {
            foreach (var cached in overloads)
            {
                if (!kinds.SequenceEqual(cached.Kinds)) continue;
                member = cached.Member;
                return InteropStatus.Ok;
            }
        }
        InteropStatus status = ScriptInteropDispatch.ResolveMethod(handle, name, kinds, out member);
        if (status != InteropStatus.Ok) return status;
        if (overloads == null) methodCache.Add(name, overloads = []);
        overloads.Add((kinds.ToArray(), member));
        return status;
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct NativeScriptInteropApi
{
    public delegate* unmanaged[Cdecl]<EnsId, uint, int, ComponentHandle*, InteropStatus> FindNativeByTypeRuntimeId;
    public delegate* unmanaged[Cdecl]<EnsId, byte*, int, int, ComponentHandle*, InteropStatus> FindNativeByName;
    public delegate* unmanaged[Cdecl]<ComponentHandle, InteropStatus> IsValid;
    public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, MemberHandle*, InteropStatus> ResolveField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, uint*, int, MemberHandle*, InteropStatus> ResolveMethod;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, InteropValueAbi*, InteropStatus> GetField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, InteropValueAbi*, InteropStatus> SetField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, InteropValueAbi*, int, InteropValueAbi*, InteropStatus> Invoke;
    public delegate* unmanaged[Cdecl]<ManagedScriptInteropApi*, InteropStatus> RegisterManagedApi;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ManagedScriptInteropApi
{
    public delegate* unmanaged[Cdecl]<EnsId, byte*, int, int, ComponentHandle*, InteropStatus> FindComponent;
    public delegate* unmanaged[Cdecl]<ComponentHandle, InteropStatus> IsValid;
    public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, MemberHandle*, InteropStatus> ResolveField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, uint*, int, MemberHandle*, InteropStatus> ResolveMethod;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, InteropValueAbi*, InteropStatus> GetField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, InteropValueAbi*, InteropStatus> SetField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, InteropValueAbi*, int, InteropValueAbi*, InteropStatus> Invoke;
    public delegate* unmanaged[Cdecl]<IntPtr, InteropStatus> HostAttached;
    public delegate* unmanaged[Cdecl]<IntPtr, InteropStatus> HostDetached;
    public delegate* unmanaged[Cdecl]<IntPtr, InteropStatus> HostEnabledChanged;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, InteropStatus> HostFieldChanged;
}

internal static unsafe class ScriptInteropDispatch
{
    private static NativeScriptInteropApi nativeApi;

    internal static void Initialize(NativeScriptInteropApi api)
    {
        nativeApi = api;
        NativeMethodRuntime.Initialize(default);
        NativeFieldRuntime.Initialize(default);
    }

    internal static ComponentProxy? FindNative(EnsId ens, string typeName, int occurrence)
    {
        if (nativeApi.FindNativeByName == null || occurrence < 0 || string.IsNullOrWhiteSpace(typeName)) return null;
        byte[] bytes = InteropText.EncodeUtf8(typeName);
        ComponentHandle handle;
        fixed (byte* pointer = bytes)
        {
            return nativeApi.FindNativeByName(ens, pointer, bytes.Length, occurrence, &handle) == InteropStatus.Ok
                ? new ComponentProxy(handle)
                : null;
        }
    }

    internal static ComponentProxy? FindManaged(EnsId ens, string typeName, int occurrence)
    {
        InteropStatus enter = ManagedScriptInterop.EnterLocalCall();
        if (enter != InteropStatus.Ok) return null;
        try
        {
            return ManagedScriptInterop.FindComponent(ens, typeName, occurrence, out ComponentHandle handle) == InteropStatus.Ok
                ? new ComponentProxy(handle)
                : null;
        }
        finally { ManagedScriptInterop.ExitLocalCall(); }
    }

    internal static InteropStatus IsValid(ComponentHandle handle)
    {
        if (handle.Domain == ComponentDomain.Managed)
        {
            InteropStatus enter = ManagedScriptInterop.EnterLocalCall();
            if (enter != InteropStatus.Ok) return enter;
            try { return ManagedScriptInterop.IsValid(handle); }
            finally { ManagedScriptInterop.ExitLocalCall(); }
        }
        return nativeApi.IsValid == null ? InteropStatus.StaleHandle : nativeApi.IsValid(handle);
    }

    internal static InteropStatus ResolveField(ComponentHandle handle, string name, out MemberHandle member)
    {
        if (handle.Domain == ComponentDomain.Managed)
        {
            InteropStatus enter = ManagedScriptInterop.EnterLocalCall();
            if (enter != InteropStatus.Ok)
            {
                member = default;
                return enter;
            }
            try { return ManagedScriptInterop.ResolveField(handle, name, out member); }
            finally { ManagedScriptInterop.ExitLocalCall(); }
        }
        member = default;
        if (nativeApi.ResolveField == null) return InteropStatus.StaleHandle;
        byte[] bytes = InteropText.EncodeUtf8(name);
        fixed (byte* pointer = bytes)
        fixed (MemberHandle* output = &member)
        {
            return nativeApi.ResolveField(handle, pointer, bytes.Length, output);
        }
    }

    internal static InteropStatus ResolveMethod(ComponentHandle handle, string name, ReadOnlySpan<InteropValueKind> kinds, out MemberHandle member)
    {
        if (handle.Domain == ComponentDomain.Managed)
        {
            InteropStatus enter = ManagedScriptInterop.EnterLocalCall();
            if (enter != InteropStatus.Ok)
            {
                member = default;
                return enter;
            }
            try { return ManagedScriptInterop.ResolveMethod(handle, name, kinds, out member); }
            finally { ManagedScriptInterop.ExitLocalCall(); }
        }
        member = default;
        if (nativeApi.ResolveMethod == null) return InteropStatus.StaleHandle;

        byte[] bytes = InteropText.EncodeUtf8(name);
        Span<uint> rawKinds = kinds.Length <= 16 ? stackalloc uint[kinds.Length] : new uint[kinds.Length];
        for (int index = 0; index < kinds.Length; ++index) rawKinds[index] = (uint)kinds[index];
        fixed (byte* namePointer = bytes)
        fixed (uint* kindsPointer = rawKinds)
        fixed (MemberHandle* output = &member)
        {
            return nativeApi.ResolveMethod(handle, namePointer, bytes.Length, kindsPointer, rawKinds.Length, output);
        }
    }

    internal static InteropStatus GetField(ComponentHandle handle, MemberHandle member, out InteropValue value)
    {
        if (handle.Domain == ComponentDomain.Managed)
        {
            InteropStatus enter = ManagedScriptInterop.EnterLocalCall();
            if (enter != InteropStatus.Ok)
            {
                value = default;
                return enter;
            }
            try { return ManagedScriptInterop.GetField(handle, member, out value); }
            finally { ManagedScriptInterop.ExitLocalCall(); }
        }
        value = default;
        if (nativeApi.GetField == null) return InteropStatus.StaleHandle;
        InteropValueAbi abi = default;
        InteropStatus status = nativeApi.GetField(handle, member, &abi);
        if (status != InteropStatus.Ok) return status;
        return InteropAbiConverter.TryDecode(abi, out value) ? InteropStatus.Ok : InteropStatus.UnsupportedType;
    }

    internal static InteropStatus SetField(ComponentHandle handle, MemberHandle member, InteropValue value)
    {
        if (handle.Domain == ComponentDomain.Managed)
        {
            InteropStatus enter = ManagedScriptInterop.EnterLocalCall();
            if (enter != InteropStatus.Ok) return enter;
            try { return ManagedScriptInterop.SetField(handle, member, value); }
            finally { ManagedScriptInterop.ExitLocalCall(); }
        }
        if (nativeApi.SetField == null) return InteropStatus.StaleHandle;
        using InteropAbiConverter.EncodedValue encoded = InteropAbiConverter.Encode(value);
        if (!encoded.Success) return InteropStatus.UnsupportedType;
        InteropValueAbi abi = encoded.Abi;
        return nativeApi.SetField(handle, member, &abi);
    }

    internal static InteropStatus Invoke(ComponentHandle handle, MemberHandle member, ReadOnlySpan<InteropValue> arguments, out InteropValue result)
    {
        if (handle.Domain == ComponentDomain.Managed)
        {
            InteropStatus enter = ManagedScriptInterop.EnterLocalCall();
            if (enter != InteropStatus.Ok)
            {
                result = default;
                return enter;
            }
            try { return ManagedScriptInterop.Invoke(handle, member, arguments, out result); }
            finally { ManagedScriptInterop.ExitLocalCall(); }
        }
        result = default;
        if (nativeApi.Invoke == null) return InteropStatus.StaleHandle;

        //无参数调用直接传空指针
        if (arguments.IsEmpty)
        {
            InteropValueAbi emptyOutput = default;
            InteropStatus emptyStatus = nativeApi.Invoke(handle, member, null, 0, &emptyOutput);
            if (emptyStatus != InteropStatus.Ok) return emptyStatus;
            return InteropAbiConverter.TryDecode(emptyOutput, out result) ? InteropStatus.Ok : InteropStatus.UnsupportedType;
        }

        Span<InteropValueAbi> values = arguments.Length <= 16 ? stackalloc InteropValueAbi[arguments.Length] : new InteropValueAbi[arguments.Length];
        Span<InteropAbiConverter.EncodedValue> resources = arguments.Length <= 16
            ? stackalloc InteropAbiConverter.EncodedValue[arguments.Length] : new InteropAbiConverter.EncodedValue[arguments.Length];
        using InteropAbiConverter.EncodedValues encoded = new(arguments, values, resources);
        if (!encoded.Success) return InteropStatus.UnsupportedType;
        InteropValueAbi output = default;
        InteropStatus status;
        fixed (InteropValueAbi* pointer = values)
            status = nativeApi.Invoke(handle, member, pointer, arguments.Length, &output);
        if (status != InteropStatus.Ok) return status;
        return InteropAbiConverter.TryDecode(output, out result) ? InteropStatus.Ok : InteropStatus.UnsupportedType;
    }

    internal static InteropStatus RegisterManaged(ManagedScriptInteropApi* api)
    {
        return nativeApi.RegisterManagedApi == null ? InteropStatus.StaleHandle : nativeApi.RegisterManagedApi(api);
    }
}

internal static unsafe class InteropAbiConverter
{
    internal struct EncodedValue : IDisposable
    {
        internal InteropValueAbi Abi;
        internal bool Success;
        private GCHandle pin;

        internal void Pin(byte[] bytes)
        {
            pin = GCHandle.Alloc(bytes, GCHandleType.Pinned);
            fixed (byte* payload = Abi.Payload)
            {
                *(IntPtr*)payload = pin.AddrOfPinnedObject();
                *(int*)(payload + 8) = bytes.Length;
            }
        }

        public void Dispose()
        {
            if (pin.IsAllocated) pin.Free();
        }
    }

    internal ref struct EncodedValues
    {
        private Span<EncodedValue> resources;
        internal bool Success { get; }

        internal EncodedValues(ReadOnlySpan<InteropValue> input, Span<InteropValueAbi> values, Span<EncodedValue> storage)
        {
            resources = storage;
            resources.Clear();
            bool success = true;
            try
            {
                for (int index = 0; index < input.Length; ++index)
                {
                    resources[index] = Encode(input[index]);
                    success &= resources[index].Success;
                    values[index] = resources[index].Abi;
                }
                Success = success;
            }
            catch
            {
                Dispose();
                throw;
            }
        }

        public void Dispose()
        {
            foreach (ref EncodedValue resource in resources) resource.Dispose();
        }
    }

    //复制内联 ABI，仅为字符串分配编码缓冲
    internal static EncodedValue Encode(InteropValue value)
    {
        EncodedValue result = new() { Abi = value.Abi, Success = IsSupported(value.Kind) };
        if (value.Kind is InteropValueKind.String or InteropValueKind.StringId)
        {
            value.TryGet(out string text);
            result.Pin(InteropText.EncodeUtf8(text));
        }
        return result;
    }

    private static bool IsSupported(InteropValueKind kind) => kind <= InteropValueKind.Object || kind == InteropValueKind.Vector2;

    //直接保存数值 ABI，字符串在返回前复制
    internal static bool TryDecode(InteropValueAbi abi, out InteropValue value)
    {
        InteropValueKind kind = (InteropValueKind)abi.Kind;
        value = default;
        if (!IsSupported(kind)) return false;
        if (kind is InteropValueKind.String or InteropValueKind.StringId)
        {
            byte* payload = abi.Payload;
            byte* pointer = *(byte**)payload;
            int length = *(int*)(payload + 8);
            if (length < 0 || length > 0 && pointer == null) return false;
            string text = length == 0 ? string.Empty : InteropText.DecodeUtf8(pointer, length);
            value = kind == InteropValueKind.String ? InteropValue.From(text) : InteropValue.FromStringId(text);
            return true;
        }
        value = new InteropValue(abi);
        return true;
    }
}
