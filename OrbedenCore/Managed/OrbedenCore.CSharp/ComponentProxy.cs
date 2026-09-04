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
public readonly struct InteropValue : IEquatable<InteropValue>
{
    private readonly object? data;

    public InteropValueKind Kind { get; }
    public object? Value => data;

    private InteropValue(InteropValueKind kind, object? value)
    {
        Kind = kind;
        data = value;
    }

    public static InteropValue Empty => default;
    public static InteropValue From(bool value) => new(InteropValueKind.Bool, value);
    public static InteropValue From(int value) => new(InteropValueKind.Int32, value);
    public static InteropValue From(uint value) => new(InteropValueKind.UInt32, value);
    public static InteropValue From(ulong value) => new(InteropValueKind.UInt64, value);
    public static InteropValue From(float value) => new(InteropValueKind.Float32, value);
    public static InteropValue From(string? value) => new(InteropValueKind.String, value ?? string.Empty);
    public static InteropValue FromStringId(string? value) => new(InteropValueKind.StringId, value ?? string.Empty);
    public static InteropValue From(vector3 value) => new(InteropValueKind.Vector3, value);
    public static InteropValue From(color4 value) => new(InteropValueKind.Color, value);
    public static InteropValue From(quaternion value) => new(InteropValueKind.Quaternion, value);
    public static InteropValue From(EnsId value) => new(InteropValueKind.EnsId, value);
    public static InteropValue FromObject(Object? value) => new(InteropValueKind.Object, value?.InstanceId ?? 0);
    public static InteropValue FromObjectId(int value) => new(InteropValueKind.Object, value);

    public bool TryGet<T>(out T value)
    {
        if (data is T typed)
        {
            value = typed;
            return true;
        }
        value = default!;
        return false;
    }

    public bool Equals(InteropValue other) => Kind == other.Kind && Equals(data, other.data);
    public override bool Equals(object? obj) => obj is InteropValue other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(Kind, data);
    public override string ToString() => data?.ToString() ?? string.Empty;
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
    public InteropStatus Invoke(out InteropValue result, params InteropValue[] arguments)
    {
        return Invoke(arguments.AsSpan(), out result);
    }
}

/// <summary>预解析组件字段和方法的统一代理。</summary>
public sealed class ComponentProxy
{
    private readonly ComponentHandle handle;
    private readonly Dictionary<string, MemberHandle> fieldCache = new(StringComparer.Ordinal);
    private readonly Dictionary<string, MemberHandle> methodCache = new(StringComparer.Ordinal);

    internal ComponentProxy(ComponentHandle value)
    {
        handle = value;
    }

    public bool IsValid => ScriptInteropDispatch.IsValid(handle) == InteropStatus.Ok;

    public InteropStatus ResolveField(string name)
    {
        return ResolveFieldHandle(name, out _);
    }

    public InteropStatus ResolveMethod(string name, params InteropValueKind[] parameterKinds)
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
    public InteropStatus TryResolveMethod(string name, out ComponentMethod method, params InteropValueKind[] parameterKinds)
    {
        return TryResolveMethod(name, parameterKinds.AsSpan(), out method);
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
        InteropValueKind[] kinds = new InteropValueKind[arguments.Length];
        for (int index = 0; index < arguments.Length; ++index) kinds[index] = arguments[index].Kind;
        InteropStatus status = ResolveMethodHandle(name, kinds, out MemberHandle member);
        return status == InteropStatus.Ok ? ScriptInteropDispatch.Invoke(handle, member, arguments, out result) : status;
    }

    public InteropStatus Invoke(string name, out InteropValue result, params InteropValue[] arguments)
    {
        return Invoke(name, arguments.AsSpan(), out result);
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
        StringBuilder keyBuilder = new(name);
        foreach (InteropValueKind kind in kinds) keyBuilder.Append('#').Append((uint)kind);
        string key = keyBuilder.ToString();
        if (methodCache.TryGetValue(key, out member)) return InteropStatus.Ok;

        InteropStatus status = ScriptInteropDispatch.ResolveMethod(handle, name, kinds, out member);
        if (status == InteropStatus.Ok) methodCache.Add(key, member);
        return status;
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct NativeScriptInteropApi
{
    public delegate* unmanaged[Cdecl]<EnsId, uint, int, ComponentHandle*, InteropStatus> FindNativeByTypeId;
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
    }

    internal static ComponentProxy? FindNative(EnsId ens, string typeName, int occurrence)
    {
        if (nativeApi.FindNativeByName == null || occurrence < 0 || string.IsNullOrWhiteSpace(typeName)) return null;
        byte[] bytes = Encoding.UTF8.GetBytes(typeName);
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
        byte[] bytes = Encoding.UTF8.GetBytes(name);
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

        byte[] bytes = Encoding.UTF8.GetBytes(name);
        uint[] rawKinds = new uint[kinds.Length];
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

        using InteropAbiConverter.EncodedValues encoded = InteropAbiConverter.Encode(arguments);
        if (!encoded.Success) return InteropStatus.UnsupportedType;
        InteropValueAbi output = default;
        InteropStatus status = nativeApi.Invoke(handle, member, encoded.Pointer, encoded.Count, &output);
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
    internal sealed class EncodedValue : IDisposable
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

    internal sealed class EncodedValues : IDisposable
    {
        private readonly EncodedValue[] values;
        private GCHandle pin;
        internal bool Success { get; }
        internal int Count => values.Length;
        internal InteropValueAbi* Pointer => pin.IsAllocated ? (InteropValueAbi*)pin.AddrOfPinnedObject() : null;

        internal EncodedValues(ReadOnlySpan<InteropValue> input)
        {
            values = new EncodedValue[input.Length];
            InteropValueAbi[] abiValues = new InteropValueAbi[input.Length];
            bool success = true;
            for (int index = 0; index < input.Length; ++index)
            {
                values[index] = Encode(input[index]);
                success &= values[index].Success;
                abiValues[index] = values[index].Abi;
            }
            Success = success;
            if (abiValues.Length != 0) pin = GCHandle.Alloc(abiValues, GCHandleType.Pinned);
        }

        public void Dispose()
        {
            if (pin.IsAllocated) pin.Free();
            foreach (EncodedValue value in values) value.Dispose();
        }
    }

    internal static EncodedValue Encode(InteropValue value)
    {
        EncodedValue result = new() { Success = true };
        result.Abi.Kind = (uint)value.Kind;
        fixed (byte* payload = result.Abi.Payload)
        {
            switch (value.Kind)
            {
                case InteropValueKind.Empty: break;
                case InteropValueKind.Bool: if (value.TryGet(out bool b)) *payload = b ? (byte)1 : (byte)0; else result.Success = false; break;
                case InteropValueKind.Int32: if (value.TryGet(out int i)) *(int*)payload = i; else result.Success = false; break;
                case InteropValueKind.UInt32: if (value.TryGet(out uint u)) *(uint*)payload = u; else result.Success = false; break;
                case InteropValueKind.UInt64: if (value.TryGet(out ulong ul)) *(ulong*)payload = ul; else result.Success = false; break;
                case InteropValueKind.Float32: if (value.TryGet(out float f)) *(float*)payload = f; else result.Success = false; break;
                case InteropValueKind.String:
                case InteropValueKind.StringId:
                    if (value.TryGet(out string text)) result.Pin(Encoding.UTF8.GetBytes(text)); else result.Success = false;
                    break;
                case InteropValueKind.Vector3: if (value.TryGet(out vector3 v)) *(vector3*)payload = v; else result.Success = false; break;
                case InteropValueKind.Color: if (value.TryGet(out color4 c)) *(color4*)payload = c; else result.Success = false; break;
                case InteropValueKind.Quaternion: if (value.TryGet(out quaternion q)) *(quaternion*)payload = q; else result.Success = false; break;
                case InteropValueKind.EnsId: if (value.TryGet(out EnsId ens)) *(EnsId*)payload = ens; else result.Success = false; break;
                case InteropValueKind.Object: if (value.TryGet(out int objectId)) *(int*)payload = objectId; else result.Success = false; break;
                default: result.Success = false; break;
            }
        }
        return result;
    }

    internal static EncodedValues Encode(ReadOnlySpan<InteropValue> values) => new(values);

    internal static bool TryDecode(InteropValueAbi abi, out InteropValue value)
    {
        byte* payload = abi.Payload;
            InteropValueKind kind = (InteropValueKind)abi.Kind;
            value = kind switch
            {
                InteropValueKind.Empty => InteropValue.Empty,
                InteropValueKind.Bool => InteropValue.From(*payload != 0),
                InteropValueKind.Int32 => InteropValue.From(*(int*)payload),
                InteropValueKind.UInt32 => InteropValue.From(*(uint*)payload),
                InteropValueKind.UInt64 => InteropValue.From(*(ulong*)payload),
                InteropValueKind.Float32 => InteropValue.From(*(float*)payload),
                InteropValueKind.String => InteropValue.From(ReadUtf8(payload)),
                InteropValueKind.StringId => InteropValue.FromStringId(ReadUtf8(payload)),
                InteropValueKind.Vector3 => InteropValue.From(*(vector3*)payload),
                InteropValueKind.Color => InteropValue.From(*(color4*)payload),
                InteropValueKind.Quaternion => InteropValue.From(*(quaternion*)payload),
                InteropValueKind.EnsId => InteropValue.From(*(EnsId*)payload),
                InteropValueKind.Object => InteropValue.FromObjectId(*(int*)payload),
                _ => default,
            };
            return kind <= InteropValueKind.Object;
    }

    private static string ReadUtf8(byte* payload)
    {
        byte* pointer = *(byte**)payload;
        int length = *(int*)(payload + 8);
        return pointer == null || length <= 0 ? string.Empty : Encoding.UTF8.GetString(pointer, length);
    }
}
