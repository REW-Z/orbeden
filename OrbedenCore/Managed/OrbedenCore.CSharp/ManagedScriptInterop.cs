using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

internal sealed class ManagedFieldMetadata
{
    internal string Name = string.Empty;
    internal Type FieldType = null!;
    internal InteropValueKind Kind;
    internal bool InspectorVisible;
    internal Func<ScriptBehaviour, object?> Getter = null!;
    internal Action<ScriptBehaviour, object?> Setter = null!;
}

internal sealed class ManagedMethodMetadata
{
    internal MethodInfo Method = null!;
    internal InteropValueKind ReturnKind;
    internal InteropValueKind[] ParameterKinds = [];
    internal Type[] ParameterTypes = [];
}

internal sealed class ManagedTypeMetadata
{
    internal readonly Dictionary<string, ManagedFieldMetadata> Fields = new(StringComparer.Ordinal);
    internal readonly List<ManagedFieldMetadata> InspectorFields = [];
    internal readonly Dictionary<string, List<ManagedMethodMetadata>> Methods = new(StringComparer.Ordinal);
}

/// <summary>程序集加载时建立一次、运行期只查表的托管脚本元数据缓存。</summary>
internal static partial class ManagedTypeMetadataCache
{
    private static readonly Dictionary<Type, ManagedTypeMetadata> cache = [];

    internal static ManagedTypeMetadata Get(Type type)
    {
        if (cache.TryGetValue(type, out ManagedTypeMetadata? metadata)) return metadata;
        metadata = Build(type);
        cache.Add(type, metadata);
        return metadata;
    }

    internal static void Clear()
    {
        cache.Clear();
    }

    internal static bool TryGetKind(Type type, out InteropValueKind kind)
    {
        Type valueType = Nullable.GetUnderlyingType(type) ?? type;
        if (valueType == typeof(bool)) kind = InteropValueKind.Bool;
        else if (valueType == typeof(int)
            || valueType.IsEnum && Enum.GetUnderlyingType(valueType) == typeof(int)) kind = InteropValueKind.Int32;
        else if (valueType == typeof(uint)
            || valueType.IsEnum && Enum.GetUnderlyingType(valueType) == typeof(uint)) kind = InteropValueKind.UInt32;
        else if (valueType == typeof(ulong)) kind = InteropValueKind.UInt64;
        else if (valueType == typeof(float)) kind = InteropValueKind.Float32;
        else if (valueType == typeof(string)) kind = InteropValueKind.String;
        else if (valueType == typeof(vector3)) kind = InteropValueKind.Vector3;
        else if (valueType == typeof(color4)) kind = InteropValueKind.Color;
        else if (valueType == typeof(quaternion)) kind = InteropValueKind.Quaternion;
        else if (valueType == typeof(EnsId)) kind = InteropValueKind.EnsId;
        else if (typeof(Object).IsAssignableFrom(valueType)) kind = InteropValueKind.Object;
        else
        {
            kind = InteropValueKind.Empty;
            return false;
        }
        return true;
    }

    internal static bool TryToInterop(object? input, Type declaredType, out InteropValue value)
    {
        if (!TryGetKind(declaredType, out InteropValueKind kind))
        {
            value = default;
            return false;
        }

        if (input == null)
        {
            value = kind == InteropValueKind.String ? InteropValue.From(string.Empty) : InteropValue.FromObject(null);
            return kind is InteropValueKind.String or InteropValueKind.Object;
        }

        value = kind switch
        {
            InteropValueKind.Bool => InteropValue.From((bool)input),
            InteropValueKind.Int32 => InteropValue.From(declaredType.IsEnum ? Convert.ToInt32(input, CultureInfo.InvariantCulture) : (int)input),
            InteropValueKind.UInt32 => InteropValue.From(declaredType.IsEnum ? Convert.ToUInt32(input, CultureInfo.InvariantCulture) : (uint)input),
            InteropValueKind.UInt64 => InteropValue.From((ulong)input),
            InteropValueKind.Float32 => InteropValue.From((float)input),
            InteropValueKind.String => InteropValue.From((string)input),
            InteropValueKind.Vector3 => InteropValue.From((vector3)input),
            InteropValueKind.Color => InteropValue.From((color4)input),
            InteropValueKind.Quaternion => InteropValue.From((quaternion)input),
            InteropValueKind.EnsId => InteropValue.From((EnsId)input),
            InteropValueKind.Object => InteropValue.FromObject((Object)input),
            _ => default,
        };
        return true;
    }

    internal static bool TryFromInterop(InteropValue value, Type targetType, out object? result)
    {
        result = null;
        if (!TryGetKind(targetType, out InteropValueKind expected) || expected != value.Kind) return false;
        Type valueType = Nullable.GetUnderlyingType(targetType) ?? targetType;
        if (valueType.IsEnum)
        {
            if (expected == InteropValueKind.Int32 && value.TryGet(out int signed))
            {
                result = Enum.ToObject(valueType, signed);
                return true;
            }
            if (expected == InteropValueKind.UInt32 && value.TryGet(out uint unsigned))
            {
                result = Enum.ToObject(valueType, unsigned);
                return true;
            }
            return false;
        }
        if (expected == InteropValueKind.Object)
        {
            if (!value.TryGet(out int objectId)) return false;
            Object? objectValue = Object.FindCachedObject(objectId);
            if (objectId != 0 && (objectValue == null || !valueType.IsInstanceOfType(objectValue))) return false;
            result = objectValue;
            return true;
        }

        result = value.Value;
        return result == null ? !valueType.IsValueType : valueType.IsInstanceOfType(result);
    }

    internal static void ApplyHostFields(ScriptBehaviour script, IntPtr host)
    {
        ManagedTypeMetadata metadata = Get(script.GetType());
        foreach ((string name, ManagedHostField stored) in ScriptBehaviour.ReadHostFields(host))
        {
            if (!metadata.Fields.TryGetValue(name, out ManagedFieldMetadata? field)) continue;
            if (!TryParseSerialized(field.Kind, stored.Value, out InteropValue value)) continue;
            if (!TryFromInterop(value, field.FieldType, out object? converted)) continue;
            field.Setter(script, converted);
        }
    }

    private static ManagedTypeMetadata Build(Type type)
    {
        ManagedTypeMetadata metadata = new();
        Stack<Type> chain = new();
        for (Type? current = type; current != null && typeof(ScriptBehaviour).IsAssignableFrom(current); current = current.BaseType) chain.Push(current);

        while (chain.TryPop(out Type? current))
        {
            foreach (FieldInfo field in current.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (field.IsStatic || field.IsInitOnly) continue;
                bool serialized = field.IsPublic || field.GetCustomAttribute<SerializeFieldAttribute>() != null;
                if (!serialized || !TryGetKind(field.FieldType, out InteropValueKind kind)) continue;

                ManagedFieldMetadata entry = new()
                {
                    Name = field.Name,
                    FieldType = field.FieldType,
                    Kind = kind,
                    InspectorVisible = field.GetCustomAttribute<HideInInspectorAttribute>() == null,
                    Getter = script => field.GetValue(script),
                    Setter = (script, value) => field.SetValue(script, value),
                };
                metadata.Fields[field.Name] = entry;
            }
        }

        //enabled 是有业务副作用的属性，作为字段语义暴露但始终走 setter。
        metadata.Fields["enabled"] = new ManagedFieldMetadata
        {
            Name = "enabled",
            FieldType = typeof(bool),
            Kind = InteropValueKind.Bool,
            InspectorVisible = true,
            Getter = script => script.enabled,
            Setter = (script, value) => script.enabled = value is bool enabled && enabled,
        };

        metadata.InspectorFields.AddRange(metadata.Fields.Values.Where(field => field.InspectorVisible));
        metadata.InspectorFields.Sort((left, right) => StringComparer.Ordinal.Compare(left.Name, right.Name));

        foreach (MethodInfo method in type.GetMethods(BindingFlags.Instance | BindingFlags.Public))
        {
            if (method.IsStatic || method.IsGenericMethodDefinition || method.IsSpecialName || method.DeclaringType == typeof(object)) continue;
            if (method.Name is "OnStart" or "OnUpdate" or "OnFixedUpdate" or
                "OnLateUpdate" or "OnDrawGUI" or "OnEnd")
                continue;
            ParameterInfo[] parameters = method.GetParameters();
            if (parameters.Any(parameter => parameter.ParameterType.IsByRef || parameter.IsOut)) continue;
            if (!TryGetMethodReturnKind(method.ReturnType, out InteropValueKind returnKind)) continue;

            InteropValueKind[] kinds = new InteropValueKind[parameters.Length];
            Type[] parameterTypes = new Type[parameters.Length];
            bool supported = true;
            for (int index = 0; index < parameters.Length; ++index)
            {
                parameterTypes[index] = parameters[index].ParameterType;
                if (!TryGetKind(parameterTypes[index], out kinds[index]))
                {
                    supported = false;
                    break;
                }
            }
            if (!supported) continue;

            if (!metadata.Methods.TryGetValue(method.Name, out List<ManagedMethodMetadata>? overloads))
            {
                overloads = [];
                metadata.Methods.Add(method.Name, overloads);
            }
            overloads.Add(new ManagedMethodMetadata { Method = method, ReturnKind = returnKind, ParameterKinds = kinds, ParameterTypes = parameterTypes });
        }
        return metadata;
    }

    private static bool TryGetMethodReturnKind(Type type, out InteropValueKind kind)
    {
        if (type == typeof(void))
        {
            kind = InteropValueKind.Empty;
            return true;
        }
        return TryGetKind(type, out kind);
    }

    private static bool TryParseSerialized(InteropValueKind kind, string text, out InteropValue value)
    {
        switch (kind)
        {
            case InteropValueKind.Bool when text is "true" or "false" or "1" or "0": value = InteropValue.From(text is "true" or "1"); return true;
            case InteropValueKind.Int32 when int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int integer): value = InteropValue.From(integer); return true;
            case InteropValueKind.UInt32 when uint.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out uint unsigned): value = InteropValue.From(unsigned); return true;
            case InteropValueKind.UInt64 when ulong.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out ulong unsignedLong): value = InteropValue.From(unsignedLong); return true;
            case InteropValueKind.Float32 when float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float number): value = InteropValue.From(number); return true;
            case InteropValueKind.String: value = InteropValue.From(text); return true;
            case InteropValueKind.Vector3 when TryParseFloats(text, 3, out float[] vector): value = InteropValue.From(new vector3(vector[0], vector[1], vector[2])); return true;
            case InteropValueKind.Color when TryParseFloats(text, 4, out float[] color): value = InteropValue.From(new color4(color[0], color[1], color[2], color[3])); return true;
            case InteropValueKind.Quaternion when TryParseFloats(text, 4, out float[] quaternion): value = InteropValue.From(new quaternion(quaternion[0], quaternion[1], quaternion[2], quaternion[3])); return true;
            case InteropValueKind.EnsId:
            case InteropValueKind.Object when int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int objectId): value = InteropValue.FromObjectId(objectId); return true;
            {
                string[] parts = text.Split(':');
                if (parts.Length == 2 && uint.TryParse(parts[0], out uint id) && uint.TryParse(parts[1], out uint version))
                {
                    value = InteropValue.From(new EnsId(id, version));
                    return true;
                }
                break;
            }
        }
        value = default;
        return false;
    }

    private static bool TryParseFloats(string text, int count, out float[] values)
    {
        string[] parts = text.Split([' ', ',', ';'], StringSplitOptions.RemoveEmptyEntries);
        values = new float[count];
        if (parts.Length != count) return false;
        for (int index = 0; index < count; ++index)
        {
            if (!float.TryParse(parts[index], NumberStyles.Float, CultureInfo.InvariantCulture, out values[index])) return false;
        }
        return true;
    }
}

internal static unsafe partial class ManagedScriptInterop
{
    private sealed class MemberEntry
    {
        internal Type DeclaringType = null!;
        internal ManagedFieldMetadata? Field;
        internal ManagedMethodMetadata? Method;
    }

    private static readonly Dictionary<ulong, MemberEntry> members = [];
    private static readonly Dictionary<string, MemberHandle> memberCache = new(StringComparer.Ordinal);
    private static ulong nextMemberSlot = 1;
    private static uint memberGeneration = 1;
    private static int mainThreadId;
    [ThreadStatic] private static int reentrancyDepth;
    [ThreadStatic] private static GCHandle stringPin;

    internal static void Initialize()
    {
        mainThreadId = Environment.CurrentManagedThreadId;
        ManagedScriptInteropApi api = new()
        {
            FindComponent = &FindComponentAbi,
            IsValid = &IsValidAbi,
            ResolveField = &ResolveFieldAbi,
            ResolveMethod = &ResolveMethodAbi,
            GetField = &GetFieldAbi,
            SetField = &SetFieldAbi,
            Invoke = &InvokeAbi,
            HostAttached = &HostAttachedAbi,
            HostDetached = &HostDetachedAbi,
            HostEnabledChanged = &HostEnabledChangedAbi,
            HostFieldChanged = &HostFieldChangedAbi,
        };
        ScriptInteropDispatch.RegisterManaged(&api);
    }

    internal static void Shutdown()
    {
        ScriptInteropDispatch.RegisterManaged(null);
        members.Clear();
        memberCache.Clear();
        nextMemberSlot = 1;
        ++memberGeneration;
        if (memberGeneration == 0) memberGeneration = 1;
        ManagedTypeMetadataCache.Clear();
        ReleaseStringResult();
    }

    internal static InteropStatus FindComponent(EnsId ens, string typeName, int occurrence, out ComponentHandle handle)
    {
        handle = default;
        if (occurrence < 0 || string.IsNullOrWhiteSpace(typeName)) return InteropStatus.InvalidArgument;
        int found = 0;
        foreach (ScriptBehaviour script in ScriptRuntimeRegistry.GetScripts(ens))
        {
            if (!string.Equals(script.GetType().FullName, typeName, StringComparison.Ordinal)) continue;
            if (found++ != occurrence) continue;
            return ScriptRuntimeRegistry.TryGetHandle(script, out handle) ? InteropStatus.Ok : InteropStatus.StaleHandle;
        }
        return InteropStatus.NotFound;
    }

    internal static InteropStatus IsValid(ComponentHandle handle)
    {
        return ScriptRuntimeRegistry.TryResolve(handle, out _) ? InteropStatus.Ok : InteropStatus.StaleHandle;
    }

    internal static InteropStatus ResolveField(ComponentHandle component, string name, out MemberHandle handle)
    {
        handle = default;
        if (!ScriptRuntimeRegistry.TryResolve(component, out ScriptBehaviour? script)) return InteropStatus.StaleHandle;
        if (string.IsNullOrEmpty(name)) return InteropStatus.InvalidArgument;
        ManagedTypeMetadata metadata = ManagedTypeMetadataCache.Get(script.GetType());
        if (!metadata.Fields.TryGetValue(name, out ManagedFieldMetadata? field)) return InteropStatus.NotFound;

        string cacheKey = $"F|{script.GetType().AssemblyQualifiedName}|{name}";
        if (memberCache.TryGetValue(cacheKey, out handle)) return InteropStatus.Ok;
        handle = AddMember(cacheKey, new MemberEntry { DeclaringType = script.GetType(), Field = field }, InteropMemberKind.Field);
        return InteropStatus.Ok;
    }

    internal static InteropStatus ResolveMethod(ComponentHandle component, string name, ReadOnlySpan<InteropValueKind> kinds, out MemberHandle handle)
    {
        handle = default;
        if (!ScriptRuntimeRegistry.TryResolve(component, out ScriptBehaviour? script)) return InteropStatus.StaleHandle;
        ManagedTypeMetadata metadata = ManagedTypeMetadataCache.Get(script.GetType());
        if (!metadata.Methods.TryGetValue(name, out List<ManagedMethodMetadata>? methods)) return InteropStatus.NotFound;

        ManagedMethodMetadata? match = null;
        foreach (ManagedMethodMetadata method in methods)
        {
            if (!method.ParameterKinds.AsSpan().SequenceEqual(kinds)) continue;
            if (match != null) return InteropStatus.AmbiguousMethod;
            match = method;
        }
        if (match == null) return InteropStatus.NotFound;

        string signature = string.Join(',', kinds.ToArray().Select(kind => ((uint)kind).ToString(CultureInfo.InvariantCulture)));
        string cacheKey = $"M|{script.GetType().AssemblyQualifiedName}|{name}|{signature}";
        if (memberCache.TryGetValue(cacheKey, out handle)) return InteropStatus.Ok;
        handle = AddMember(cacheKey, new MemberEntry { DeclaringType = script.GetType(), Method = match }, InteropMemberKind.Method);
        return InteropStatus.Ok;
    }

    internal static InteropStatus GetField(ComponentHandle component, MemberHandle member, out InteropValue value)
    {
        value = default;
        if (!TryResolveMember(component, member, InteropMemberKind.Field, out ScriptBehaviour? script, out MemberEntry? entry)) return InteropStatus.StaleHandle;
        try
        {
            ManagedFieldMetadata field = entry.Field!;
            return ManagedTypeMetadataCache.TryToInterop(field.Getter(script), field.FieldType, out value)
                ? InteropStatus.Ok
                : InteropStatus.UnsupportedType;
        }
        catch { return InteropStatus.InvocationFailed; }
    }

    internal static InteropStatus SetField(ComponentHandle component, MemberHandle member, InteropValue value)
    {
        if (!TryResolveMember(component, member, InteropMemberKind.Field, out ScriptBehaviour? script, out MemberEntry? entry)) return InteropStatus.StaleHandle;
        ManagedFieldMetadata field = entry.Field!;
        if (!ManagedTypeMetadataCache.TryFromInterop(value, field.FieldType, out object? converted)) return InteropStatus.TypeMismatch;
        try
        {
            field.Setter(script, converted);
            if (!ManagedTypeMetadataCache.WriteHostField(script, script.NativePtr, field))
                return InteropStatus.InvocationFailed;
            return InteropStatus.Ok;
        }
        catch { return InteropStatus.InvocationFailed; }
    }

    internal static InteropStatus Invoke(ComponentHandle component, MemberHandle member, ReadOnlySpan<InteropValue> arguments, out InteropValue result)
    {
        result = default;
        if (!TryResolveMember(component, member, InteropMemberKind.Method, out ScriptBehaviour? script, out MemberEntry? entry)) return InteropStatus.StaleHandle;
        ManagedMethodMetadata method = entry.Method!;
        if (arguments.Length != method.ParameterTypes.Length) return InteropStatus.TypeMismatch;

        object?[] converted = new object?[arguments.Length];
        for (int index = 0; index < arguments.Length; ++index)
        {
            if (!ManagedTypeMetadataCache.TryFromInterop(arguments[index], method.ParameterTypes[index], out converted[index])) return InteropStatus.TypeMismatch;
        }
        try
        {
            object? returnValue = method.Method.Invoke(script, converted);
            if (method.ReturnKind == InteropValueKind.Empty)
            {
                result = InteropValue.Empty;
                return InteropStatus.Ok;
            }
            return ManagedTypeMetadataCache.TryToInterop(returnValue, method.Method.ReturnType, out result)
                ? InteropStatus.Ok
                : InteropStatus.UnsupportedType;
        }
        catch { return InteropStatus.InvocationFailed; }
    }

    private static MemberHandle AddMember(string key, MemberEntry entry, InteropMemberKind kind)
    {
        ulong slot = nextMemberSlot++;
        members.Add(slot, entry);
        MemberHandle handle = new(ComponentDomain.Managed, kind, slot, memberGeneration);
        memberCache.Add(key, handle);
        return handle;
    }

    private static bool TryResolveMember(ComponentHandle component, MemberHandle member, InteropMemberKind kind, [NotNullWhen(true)] out ScriptBehaviour? script, [NotNullWhen(true)] out MemberEntry? entry)
    {
        entry = null;
        if (!ScriptRuntimeRegistry.TryResolve(component, out script)) return false;
        if (member.Domain != ComponentDomain.Managed || member.Kind != kind || member.Generation != memberGeneration) return false;
        if (!members.TryGetValue(member.Slot, out entry) || !entry.DeclaringType.IsInstanceOfType(script)) return false;
        return true;
    }

    private static InteropStatus EnterCall()
    {
        ReleaseStringResult();
        if (Environment.CurrentManagedThreadId != mainThreadId) return InteropStatus.WrongThread;
        if (reentrancyDepth >= 32) return InteropStatus.ReentrancyLimit;
        ++reentrancyDepth;
        return InteropStatus.Ok;
    }

    internal static InteropStatus EnterLocalCall()
    {
        return EnterCall();
    }

    private static void ExitCall()
    {
        --reentrancyDepth;
    }


    internal static void ExitLocalCall()
    {
        ExitCall();
    }
    private static void ReleaseStringResult()
    {
        if (stringPin.IsAllocated) stringPin.Free();
    }

    private static bool EncodeResult(InteropValue value, InteropValueAbi* output)
    {
        if (output == null) return false;
        using InteropAbiConverter.EncodedValue encoded = InteropAbiConverter.Encode(value);
        if (!encoded.Success) return false;
        *output = encoded.Abi;
        if (value.Kind is InteropValueKind.String or InteropValueKind.StringId)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value.ToString());
            stringPin = GCHandle.Alloc(bytes, GCHandleType.Pinned);
            byte* payload = output->Payload;
                *(IntPtr*)payload = stringPin.AddrOfPinnedObject();
                *(int*)(payload + 8) = bytes.Length;
        }
        return true;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus FindComponentAbi(EnsId ens, byte* name, int length, int occurrence, ComponentHandle* output)
    {
        InteropStatus enter = EnterCall();
        if (enter != InteropStatus.Ok) return enter;
        try
        {
            if (name == null || length <= 0 || output == null) return InteropStatus.InvalidArgument;
            string typeName = Encoding.UTF8.GetString(name, length);
            InteropStatus status = FindComponent(ens, typeName, occurrence, out ComponentHandle handle);
            if (status == InteropStatus.Ok) *output = handle;
            return status;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus IsValidAbi(ComponentHandle handle)
    {
        InteropStatus enter = EnterCall();
        if (enter != InteropStatus.Ok) return enter;
        try { return IsValid(handle); }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus ResolveFieldAbi(ComponentHandle component, byte* name, int length, MemberHandle* output)
    {
        InteropStatus enter = EnterCall();
        if (enter != InteropStatus.Ok) return enter;
        try
        {
            if (name == null || length <= 0 || output == null) return InteropStatus.InvalidArgument;
            InteropStatus status = ResolveField(component, Encoding.UTF8.GetString(name, length), out MemberHandle member);
            if (status == InteropStatus.Ok) *output = member;
            return status;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus ResolveMethodAbi(ComponentHandle component, byte* name, int length, uint* rawKinds, int count, MemberHandle* output)
    {
        InteropStatus enter = EnterCall();
        if (enter != InteropStatus.Ok) return enter;
        try
        {
            if (name == null || length <= 0 || count < 0 || count > 0 && rawKinds == null || output == null) return InteropStatus.InvalidArgument;
            InteropValueKind[] kinds = new InteropValueKind[count];
            for (int index = 0; index < count; ++index) kinds[index] = (InteropValueKind)rawKinds[index];
            InteropStatus status = ResolveMethod(component, Encoding.UTF8.GetString(name, length), kinds, out MemberHandle member);
            if (status == InteropStatus.Ok) *output = member;
            return status;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus GetFieldAbi(ComponentHandle component, MemberHandle member, InteropValueAbi* output)
    {
        InteropStatus enter = EnterCall();
        if (enter != InteropStatus.Ok) return enter;
        try
        {
            InteropStatus status = GetField(component, member, out InteropValue value);
            return status != InteropStatus.Ok ? status : EncodeResult(value, output) ? InteropStatus.Ok : InteropStatus.UnsupportedType;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus SetFieldAbi(ComponentHandle component, MemberHandle member, InteropValueAbi* input)
    {
        InteropStatus enter = EnterCall();
        if (enter != InteropStatus.Ok) return enter;
        try
        {
            if (input == null) return InteropStatus.InvalidArgument;
            return InteropAbiConverter.TryDecode(*input, out InteropValue value) ? SetField(component, member, value) : InteropStatus.UnsupportedType;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus InvokeAbi(ComponentHandle component, MemberHandle member, InteropValueAbi* input, int count, InteropValueAbi* output)
    {
        InteropStatus enter = EnterCall();
        if (enter != InteropStatus.Ok) return enter;
        try
        {
            if (count < 0 || count > 0 && input == null || output == null) return InteropStatus.InvalidArgument;
            InteropValue[] arguments = new InteropValue[count];
            for (int index = 0; index < count; ++index)
            {
                if (!InteropAbiConverter.TryDecode(input[index], out arguments[index])) return InteropStatus.UnsupportedType;
            }
            InteropStatus status = Invoke(component, member, arguments, out InteropValue result);
            return status != InteropStatus.Ok ? status : EncodeResult(result, output) ? InteropStatus.Ok : InteropStatus.UnsupportedType;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }
}
