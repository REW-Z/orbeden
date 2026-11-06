using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

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
public enum NativeValueKind : uint
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
    Pointer,
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
    ValueMethod,
    ValueField,
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
internal unsafe struct DynamicValueAbi
{
    public uint Kind;
    public uint Reserved;
    public fixed byte Payload[16];
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
    public InteropStatus Invoke(ReadOnlySpan<object?> arguments, out object? result)
    {
        return ScriptInteropDispatch.Invoke(component, method, arguments, out result);
    }

    /// <summary>使用已解析的方法句柄同步调用。</summary>
    public InteropStatus Invoke(out object? result, params ReadOnlySpan<object?> arguments)
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
    public NativeValueKind ReturnKind { get; }

    /// <summary>参数种类，按声明顺序。</summary>
    public IReadOnlyList<NativeValueKind> ParameterKinds { get; }

    internal BindableMethod(string name, NativeValueKind returnKind, NativeValueKind[] parameterKinds)
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
    private readonly Dictionary<string, List<(NativeValueKind[] Kinds, MemberHandle Member)>> methodCache = new(StringComparer.Ordinal);

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

    public InteropStatus ResolveMethod(string name, params ReadOnlySpan<NativeValueKind> parameterKinds)
    {
        return ResolveMethodHandle(name, parameterKinds, out _);
    }

    /// <summary>解析一次方法名和参数签名，返回可重复调用的方法对象。</summary>
    public InteropStatus TryResolveMethod(string name, ReadOnlySpan<NativeValueKind> parameterKinds, out ComponentMethod method)
    {
        InteropStatus status = ResolveMethodHandle(name, parameterKinds, out MemberHandle member);
        method = status == InteropStatus.Ok ? new ComponentMethod(handle, member) : default;
        return status;
    }

    /// <summary>解析一次方法名和参数签名，返回可重复调用的方法对象。</summary>
    public InteropStatus TryResolveMethod(string name, out ComponentMethod method, params ReadOnlySpan<NativeValueKind> parameterKinds)
    {
        return TryResolveMethod(name, parameterKinds, out method);
    }

    public InteropStatus TryGetField(string name, out object? value)
    {
        value = default;
        InteropStatus status = ResolveFieldHandle(name, out MemberHandle member);
        return status == InteropStatus.Ok ? ScriptInteropDispatch.GetField(handle, member, out value) : status;
    }

    public InteropStatus SetField(string name, object? value)
    {
        InteropStatus status = ResolveFieldHandle(name, out MemberHandle member);
        return status == InteropStatus.Ok ? ScriptInteropDispatch.SetField(handle, member, value) : status;
    }

    public InteropStatus Invoke(string name, ReadOnlySpan<object?> arguments, out object? result)
    {
        result = default;
        Span<NativeValueKind> kinds = arguments.Length <= 16 ? stackalloc NativeValueKind[arguments.Length] : new NativeValueKind[arguments.Length];
        for (int index = 0; index < arguments.Length; ++index) kinds[index] = arguments[index].GetValueKind();
        InteropStatus status = ResolveMethodHandle(name, kinds, out MemberHandle member);
        return status == InteropStatus.Ok ? ScriptInteropDispatch.Invoke(handle, member, arguments, out result) : status;
    }

    public InteropStatus Invoke(string name, out object? result, params ReadOnlySpan<object?> arguments)
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

    private InteropStatus ResolveMethodHandle(string name, ReadOnlySpan<NativeValueKind> kinds, out MemberHandle member)
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
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, DynamicValueAbi*, InteropStatus> GetField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, DynamicValueAbi*, InteropStatus> SetField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, DynamicValueAbi*, int, DynamicValueAbi*, InteropStatus> Invoke;
    public delegate* unmanaged[Cdecl]<ManagedScriptInteropApi*, InteropStatus> RegisterManagedApi;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ManagedScriptInteropApi
{
    public delegate* unmanaged[Cdecl]<EnsId, byte*, int, int, ComponentHandle*, InteropStatus> FindComponent;
    public delegate* unmanaged[Cdecl]<ComponentHandle, InteropStatus> IsValid;
    public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, MemberHandle*, InteropStatus> ResolveField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, uint*, int, MemberHandle*, InteropStatus> ResolveMethod;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, DynamicValueAbi*, InteropStatus> GetField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, DynamicValueAbi*, InteropStatus> SetField;
    public delegate* unmanaged[Cdecl]<ComponentHandle, MemberHandle, DynamicValueAbi*, int, DynamicValueAbi*, InteropStatus> Invoke;
    public delegate* unmanaged[Cdecl]<IntPtr, InteropStatus> HostAttached;
    public delegate* unmanaged[Cdecl]<IntPtr, InteropStatus> HostDetached;
    public delegate* unmanaged[Cdecl]<IntPtr, InteropStatus> HostEnabledChanged;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, InteropStatus> HostFieldChanged;
    public delegate* unmanaged[Cdecl]<ComponentHandle, byte*, int, uint, uint*, int, nint*, InteropStatus> BindTypedMethod;
    public delegate* unmanaged[Cdecl]<nint, void**, void*, InteropStatus> InvokeTypedMethod;
    public delegate* unmanaged[Cdecl]<void> CompleteTypedMethod;
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

    internal static InteropStatus ResolveMethod(ComponentHandle handle, string name, ReadOnlySpan<NativeValueKind> kinds, out MemberHandle member)
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

    internal static InteropStatus GetField(ComponentHandle handle, MemberHandle member, out object? value)
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
        DynamicValueAbi abi = default;
        InteropStatus status = nativeApi.GetField(handle, member, &abi);
        if (status != InteropStatus.Ok) return status;
        return InteropAbiConverter.TryDecode(abi, out value) ? InteropStatus.Ok : InteropStatus.UnsupportedType;
    }

    internal static InteropStatus SetField(ComponentHandle handle, MemberHandle member, object? value)
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
        DynamicValueAbi abi = encoded.Abi;
        return nativeApi.SetField(handle, member, &abi);
    }

    internal static InteropStatus Invoke(ComponentHandle handle, MemberHandle member, ReadOnlySpan<object?> arguments, out object? result)
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
            DynamicValueAbi emptyOutput = default;
            InteropStatus emptyStatus = nativeApi.Invoke(handle, member, null, 0, &emptyOutput);
            if (emptyStatus != InteropStatus.Ok) return emptyStatus;
            return InteropAbiConverter.TryDecode(emptyOutput, out result) ? InteropStatus.Ok : InteropStatus.UnsupportedType;
        }

        Span<DynamicValueAbi> values = arguments.Length <= 16 ? stackalloc DynamicValueAbi[arguments.Length] : new DynamicValueAbi[arguments.Length];
        Span<InteropAbiConverter.EncodedValue> resources = arguments.Length <= 16
            ? stackalloc InteropAbiConverter.EncodedValue[arguments.Length] : new InteropAbiConverter.EncodedValue[arguments.Length];
        using InteropAbiConverter.EncodedValues encoded = new(arguments, values, resources);
        if (!encoded.Success) return InteropStatus.UnsupportedType;
        DynamicValueAbi output = default;
        InteropStatus status;
        fixed (DynamicValueAbi* pointer = values)
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
        internal DynamicValueAbi Abi;
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

        internal EncodedValues(ReadOnlySpan<object?> input, Span<DynamicValueAbi> values, Span<EncodedValue> storage)
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

    //动态协议只在动态调用边界编码普通托管值
    internal static EncodedValue Encode(object? value)
    {
        NativeValueKind kind = value.GetValueKind();
        EncodedValue result = new() { Abi = new() { Kind = (uint)kind }, Success = IsSupported(kind) };
        if (!result.Success) return result;
        if (kind is NativeValueKind.String or NativeValueKind.StringId)
        {
            result.Pin(InteropText.EncodeUtf8(value?.ToString() ?? string.Empty));
            return result;
        }
        NativeMarshallingScope scope = new();
        try
        {
            byte* output = result.Abi.Payload;
            NativeMarshaller.Write(output, value?.GetType() ?? typeof(Object), value, ref scope);
        }
        finally { scope.Dispose(); }
        return result;
    }

    private static bool IsSupported(NativeValueKind kind) => kind <= NativeValueKind.Object || kind is NativeValueKind.Vector2 or NativeValueKind.Pointer;

    //动态结果直接还原为 CLR 值
    internal static bool TryDecode(DynamicValueAbi abi, out object? value)
    {
        NativeValueKind kind = (NativeValueKind)abi.Kind;
        value = null;
        if (!IsSupported(kind)) return false;
        if (kind == NativeValueKind.Empty) return true;
        Type type = kind switch
        {
            NativeValueKind.Bool => typeof(bool),
            NativeValueKind.Int32 => typeof(int),
            NativeValueKind.UInt32 => typeof(uint),
            NativeValueKind.UInt64 => typeof(ulong),
            NativeValueKind.Float32 => typeof(float),
            NativeValueKind.String => typeof(string),
            NativeValueKind.StringId => typeof(StringId),
            NativeValueKind.Vector2 => typeof(vector2),
            NativeValueKind.Vector3 => typeof(vector3),
            NativeValueKind.Color => typeof(color),
            NativeValueKind.Quaternion => typeof(quaternion),
            NativeValueKind.EnsId => typeof(EnsId),
            NativeValueKind.Object => typeof(Object),
            NativeValueKind.Pointer => typeof(nint),
            _ => throw new InvalidOperationException(),
        };
        if (kind is NativeValueKind.String or NativeValueKind.StringId)
        {
            NativeUtf8 view = *(NativeUtf8*)abi.Payload;
            if (view.Length < 0 || view.Length > 0 && view.Data == null) return false;
        }
        value = NativeMarshaller.Read(abi.Payload, type);
        return true;
    }
}
