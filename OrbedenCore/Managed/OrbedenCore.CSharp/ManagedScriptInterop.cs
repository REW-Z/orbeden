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
    internal NativeValueKind Kind;
    internal bool InspectorVisible;
    internal Func<Script, object?> Getter = null!;
    internal Action<Script, object?> Setter = null!;
}

internal sealed class ManagedMethodMetadata
{
    internal MethodInfo Method = null!;
    internal NativeValueKind ReturnKind;
    internal NativeValueKind[] ParameterKinds = [];
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
    internal static void Remove(Type type) => cache.Remove(type);

    internal static ManagedTypeMetadata Get([DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicMethods | DynamicallyAccessedMemberTypes.PublicFields | DynamicallyAccessedMemberTypes.NonPublicFields)] Type type)
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

    internal static bool TryGetKind(Type type, out NativeValueKind kind)
    {
        Type valueType = Nullable.GetUnderlyingType(type) ?? type;
        if (valueType == typeof(bool)) kind = NativeValueKind.Bool;
        else if (valueType == typeof(int)
            || valueType.IsEnum && Enum.GetUnderlyingType(valueType) == typeof(int)) kind = NativeValueKind.Int32;
        else if (valueType == typeof(uint)
            || valueType.IsEnum && Enum.GetUnderlyingType(valueType) == typeof(uint)) kind = NativeValueKind.UInt32;
        else if (valueType == typeof(ulong)) kind = NativeValueKind.UInt64;
        else if (valueType == typeof(float)) kind = NativeValueKind.Float32;
        else if (valueType == typeof(string) || typeof(OrbEvent).IsAssignableFrom(valueType)) kind = NativeValueKind.String;
        else if (valueType == typeof(StringId)) kind = NativeValueKind.StringId;
        else if (valueType == typeof(vector2)) kind = NativeValueKind.Vector2;
        else if (valueType == typeof(vector3)) kind = NativeValueKind.Vector3;
        else if (valueType == typeof(color)) kind = NativeValueKind.Color;
        else if (valueType == typeof(quaternion)) kind = NativeValueKind.Quaternion;
        else if (valueType == typeof(EnsId)) kind = NativeValueKind.EnsId;
        else if (typeof(Object).IsAssignableFrom(valueType)) kind = NativeValueKind.Object;
        else if (GetCollectionElementType(valueType) is Type element && TryGetKind(element, out var elementKind)
            && elementKind != NativeValueKind.Array && !typeof(OrbEvent).IsAssignableFrom(element)) kind = NativeValueKind.Array;
        else
        {
            kind = NativeValueKind.Empty;
            return false;
        }
        return true;
    }

    internal static bool TryToInterop(object? input, Type declaredType, out object? value)
    {
        if (!TryGetKind(declaredType, out NativeValueKind kind))
        {
            value = default;
            return false;
        }

        //集合由宿主持久化入口处理，不能装成只携带单个标量的代理值。
        if (input is OrbEvent eventValue) { value = eventValue.Serialize(); return true; }
        if (kind == NativeValueKind.Array) { value = default; return false; }

        if (input == null)
        {
            value = kind == NativeValueKind.String ? string.Empty : null;
            return kind is NativeValueKind.String or NativeValueKind.Object;
        }

        value = kind switch
        {
            NativeValueKind.Bool => (bool)input,
            NativeValueKind.Int32 => declaredType.IsEnum ? Convert.ToInt32(input, CultureInfo.InvariantCulture) : (int)input,
            NativeValueKind.UInt32 => declaredType.IsEnum ? Convert.ToUInt32(input, CultureInfo.InvariantCulture) : (uint)input,
            NativeValueKind.UInt64 => (ulong)input,
            NativeValueKind.Float32 => (float)input,
            NativeValueKind.String => (string)input,
            NativeValueKind.StringId => (StringId)input,
            NativeValueKind.Vector2 => (vector2)input,
            NativeValueKind.Vector3 => (vector3)input,
            NativeValueKind.Color => (color)input,
            NativeValueKind.Quaternion => (quaternion)input,
            NativeValueKind.EnsId => (EnsId)input,
            NativeValueKind.Object => (Object)input,
            _ => default,
        };
        return true;
    }

    internal static bool TryFromInterop(object? value, Type targetType, out object? result)
    {
        result = null;
        if (!TryGetKind(targetType, out NativeValueKind expected) || !NativeValues.Matches(value, expected)) return false;
        Type valueType = Nullable.GetUnderlyingType(targetType) ?? targetType;
        if (typeof(OrbEvent).IsAssignableFrom(valueType))
        {
            if (!value.TryGet(out string text)) return false;
            try
            {
                OrbEvent eventValue = valueType == typeof(OrbEvent) ? OrbEvent.Parse(text) : (OrbEvent)Activator.CreateInstance(valueType)!;
                if (!eventValue.Deserialize(text)) return false;
                result = eventValue;
                return true;
            }
            catch (Exception exception) when (exception is ArgumentException or FormatException or OverflowException) { return false; }
        }
        if (valueType.IsEnum)
        {
            if (expected == NativeValueKind.Int32 && value.TryGet(out int signed))
            {
                result = Enum.ToObject(valueType, signed);
                return true;
            }
            if (expected == NativeValueKind.UInt32 && value.TryGet(out uint unsigned))
            {
                result = Enum.ToObject(valueType, unsigned);
                return true;
            }
            return false;
        }
        if (expected == NativeValueKind.Object)
        {
            if (!value.TryGet(out int objectId)) return false;
            Object? objectValue = value as Object;
            if (objectId != 0 && (objectValue == null || !valueType.IsInstanceOfType(objectValue))) return false;
            result = objectValue;
            return true;
        }

        result = value;
        return result == null ? !valueType.IsValueType : valueType.IsInstanceOfType(result);
    }

    internal static void ApplyHostFields(Script script, IntPtr host)
    {
        ManagedTypeMetadata metadata = Get(script.GetType());
        foreach ((string name, ManagedHostField stored) in Script.ReadHostFields(host))
        {
            if (!metadata.Fields.TryGetValue(name, out ManagedFieldMetadata? field)) continue;
            if (!TryReadHostValue(script, field, stored, out object? converted)) continue;
            field.Setter(script, converted);
        }
    }

    private static ManagedTypeMetadata Build([DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicMethods | DynamicallyAccessedMemberTypes.PublicFields | DynamicallyAccessedMemberTypes.NonPublicFields)] Type type)
    {
        ManagedTypeMetadata metadata = new();
        Stack<Type> chain = new();
        for (Type? current = type; current != null && typeof(Script).IsAssignableFrom(current); current = current.BaseType) chain.Push(current);

        while (chain.TryPop(out Type? current))
        {
            foreach (FieldInfo field in current.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (field.IsStatic || field.IsInitOnly || field.Name is "domain" or "managedTypeName" or "enabled") continue;
                bool serialized = field.IsPublic || field.GetCustomAttribute<SerializeFieldAttribute>() != null;
                if (!serialized || !TryGetKind(field.FieldType, out NativeValueKind kind)) continue;

                ManagedFieldMetadata entry = new()
                {
                    Name = field.Name,
                    FieldType = field.FieldType,
                    Kind = kind,
                    InspectorVisible = field.GetCustomAttribute<HideInEditorAttribute>() == null,
                    Getter = script => field.GetValue(script),
                    Setter = (script, value) =>
                    {
                        if (field.GetValue(script) is OrbEvent current && value is OrbEvent incoming)
                        {
                            if (!current.Deserialize(incoming.Serialize())) throw new ArgumentException("OrbEvent signature mismatch.");
                        }
                        else field.SetValue(script, value);
                    },
                };
                metadata.Fields[field.Name] = entry;
            }
        }

        //enabled 是有业务副作用的属性，作为字段语义暴露但始终走 setter。
        metadata.Fields["enabled"] = new ManagedFieldMetadata
        {
            Name = "enabled",
            FieldType = typeof(bool),
            Kind = NativeValueKind.Bool,
            InspectorVisible = true,
            Getter = script => script.GetEnabled(),
            Setter = (script, value) => script.SetEnabled(value is bool enabled && enabled),
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
            if (!TryGetMethodReturnKind(method.ReturnType, out NativeValueKind returnKind)) continue;

            NativeValueKind[] kinds = new NativeValueKind[parameters.Length];
            Type[] parameterTypes = new Type[parameters.Length];
            bool supported = true;
            for (int index = 0; index < parameters.Length; ++index)
            {
                parameterTypes[index] = parameters[index].ParameterType;
                if (!TryGetKind(parameterTypes[index], out kinds[index]) || kinds[index] == NativeValueKind.Array
                    || typeof(OrbEvent).IsAssignableFrom(parameterTypes[index]))
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

    private static bool TryGetMethodReturnKind(Type type, out NativeValueKind kind)
    {
        if (type == typeof(void))
        {
            kind = NativeValueKind.Empty;
            return true;
        }
        return TryGetKind(type, out kind) && kind != NativeValueKind.Array && !typeof(OrbEvent).IsAssignableFrom(type);
    }

    private static bool TryParseSerialized(NativeValueKind kind, string text, out object? value)
    {
        switch (kind)
        {
            case NativeValueKind.Bool when text is "true" or "false" or "1" or "0": value = text is "true" or "1"; return true;
            case NativeValueKind.Int32 when int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int integer): value = integer; return true;
            case NativeValueKind.UInt32 when uint.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out uint unsigned): value = unsigned; return true;
            case NativeValueKind.UInt64 when ulong.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out ulong unsignedLong): value = unsignedLong; return true;
            case NativeValueKind.Float32 when float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float number): value = number; return true;
            case NativeValueKind.String: value = text; return true;
            case NativeValueKind.StringId: value = new StringId(text); return true;
            case NativeValueKind.Vector2 when TryParseFloats(text, 2, out float[] pair): value = new vector2(pair[0], pair[1]); return true;
            case NativeValueKind.Vector3 when TryParseFloats(text, 3, out float[] vector): value = new vector3(vector[0], vector[1], vector[2]); return true;
            case NativeValueKind.Color when TryParseFloats(text, 4, out float[] color): value = new color(color[0], color[1], color[2], color[3]); return true;
            case NativeValueKind.Quaternion when TryParseFloats(text, 4, out float[] quaternion): value = new quaternion(quaternion[0], quaternion[1], quaternion[2], quaternion[3]); return true;
            case NativeValueKind.EnsId:
            {
                string[] parts = text.Split(':');
                if (parts.Length == 2 && uint.TryParse(parts[0], out uint id) && uint.TryParse(parts[1], out uint version))
                {
                    value = new EnsId(id, version);
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
        AddDefaultTypedSignatures();
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
            BindTypedMethod = &BindTypedMethodAbi,
            InvokeTypedMethod = &InvokeTypedMethodAbi,
            CompleteTypedMethod = &CompleteTypedMethodAbi,
        };
        registeredApi = api;
        apiRegistered = true;
        ScriptInteropDispatch.RegisterManaged(&api);
    }

    internal static void Shutdown()
    {
        apiRegistered = false;
        ScriptInteropDispatch.RegisterManaged(null);
        ClearMembers();
        ManagedTypeMetadataCache.Clear();
        ReleaseStringResult();
    }

    /// <summary>清空保存了游戏 Type 的成员表，但保留已注册的原生互操作入口。</summary>
    internal static void ClearMembers()
    {
        ClearTypedBindings();
        members.Clear();
        memberCache.Clear();
        nextMemberSlot = 1;
        ++memberGeneration;
        if (memberGeneration == 0) memberGeneration = 1;
    }

    internal static InteropStatus FindComponent(EnsId ens, string typeName, int occurrence, out ComponentHandle handle)
    {
        handle = default;
        if (occurrence < 0 || string.IsNullOrWhiteSpace(typeName)) return InteropStatus.InvalidArgument;
        int found = 0;
        foreach (Script script in ScriptRuntimeRegistry.GetScripts(ens))
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
        if (!ScriptRuntimeRegistry.TryResolve(component, out Script? script)) return InteropStatus.StaleHandle;
        if (string.IsNullOrEmpty(name)) return InteropStatus.InvalidArgument;
        ManagedTypeMetadata metadata = ManagedTypeMetadataCache.Get(script.GetType());
        if (!metadata.Fields.TryGetValue(name, out ManagedFieldMetadata? field)) return InteropStatus.NotFound;

        string cacheKey = $"F|{script.GetType().AssemblyQualifiedName}|{name}";
        if (memberCache.TryGetValue(cacheKey, out handle)) return InteropStatus.Ok;
        handle = AddMember(cacheKey, new MemberEntry { DeclaringType = script.GetType(), Field = field }, InteropMemberKind.Field);
        return InteropStatus.Ok;
    }

    internal static InteropStatus ResolveMethod(ComponentHandle component, string name, ReadOnlySpan<NativeValueKind> kinds, out MemberHandle handle)
    {
        handle = default;
        if (!ScriptRuntimeRegistry.TryResolve(component, out Script? script)) return InteropStatus.StaleHandle;
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

    internal static InteropStatus GetField(ComponentHandle component, MemberHandle member, out object? value)
    {
        value = default;
        if (!TryResolveMember(component, member, InteropMemberKind.Field, out Script? script, out MemberEntry? entry)) return InteropStatus.StaleHandle;
        try
        {
            ManagedFieldMetadata field = entry.Field!;
            return ManagedTypeMetadataCache.TryToInterop(field.Getter(script), field.FieldType, out value)
                ? InteropStatus.Ok
                : InteropStatus.UnsupportedType;
        }
        catch { return InteropStatus.InvocationFailed; }
    }

    internal static InteropStatus SetField(ComponentHandle component, MemberHandle member, object? value)
    {
        if (!TryResolveMember(component, member, InteropMemberKind.Field, out Script? script, out MemberEntry? entry)) return InteropStatus.StaleHandle;
        ManagedFieldMetadata field = entry.Field!;
        if (!ManagedTypeMetadataCache.TryFromInterop(value, field.FieldType, out object? converted)) return InteropStatus.TypeMismatch;
        try
        {
            object? previous = field.Getter(script);
            field.Setter(script, converted);
            //显式编辑按用户输入写回：新值为空就是真正清空引用，丢弃该字段的保留路径。
            if (!ManagedTypeMetadataCache.WriteHostField(script, script.NativePtr, field, explicitEdit: true))
            {
                field.Setter(script, previous);
                return InteropStatus.InvocationFailed;
            }
            script.ClearUnresolvedReferences(field.Name);
            return InteropStatus.Ok;
        }
        catch { return InteropStatus.InvocationFailed; }
    }

    internal static InteropStatus Invoke(ComponentHandle component, MemberHandle member, ReadOnlySpan<object?> arguments, out object? result)
    {
        result = default;
        if (!TryResolveMember(component, member, InteropMemberKind.Method, out Script? script, out MemberEntry? entry)) return InteropStatus.StaleHandle;
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
            if (method.ReturnKind == NativeValueKind.Empty)
            {
                result = null;
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

    private static bool TryResolveMember(ComponentHandle component, MemberHandle member, InteropMemberKind kind, [NotNullWhen(true)] out Script? script, [NotNullWhen(true)] out MemberEntry? entry)
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

    private static bool EncodeResult(object? value, DynamicValueAbi* output, NativeValueKind? declaredKind = null)
    {
        if (output == null) return false;
        if (declaredKind == NativeValueKind.Empty)
        {
            *output = default;
            return true;
        }
        using InteropAbiConverter.EncodedValue encoded = InteropAbiConverter.Encode(value);
        if (!encoded.Success) return false;
        *output = encoded.Abi;
        if (value.GetValueKind() is NativeValueKind.String or NativeValueKind.StringId)
        {
            byte[] bytes = InteropText.EncodeUtf8(value?.ToString() ?? string.Empty);
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
            string typeName = InteropText.DecodeUtf8(name, length);
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
            InteropStatus status = ResolveField(component, InteropText.DecodeUtf8(name, length), out MemberHandle member);
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
            NativeValueKind[] kinds = new NativeValueKind[count];
            for (int index = 0; index < count; ++index) kinds[index] = (NativeValueKind)rawKinds[index];
            InteropStatus status = ResolveMethod(component, InteropText.DecodeUtf8(name, length), kinds, out MemberHandle member);
            if (status == InteropStatus.Ok) *output = member;
            return status;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus GetFieldAbi(ComponentHandle component, MemberHandle member, DynamicValueAbi* output)
    {
        InteropStatus enter = EnterCall();
        if (enter != InteropStatus.Ok) return enter;
        try
        {
            InteropStatus status = GetField(component, member, out object? value);
            return status != InteropStatus.Ok ? status : EncodeResult(value, output) ? InteropStatus.Ok : InteropStatus.UnsupportedType;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus SetFieldAbi(ComponentHandle component, MemberHandle member, DynamicValueAbi* input)
    {
        InteropStatus enter = EnterCall();
        if (enter != InteropStatus.Ok) return enter;
        try
        {
            if (input == null) return InteropStatus.InvalidArgument;
            return InteropAbiConverter.TryDecode(*input, out object? value) ? SetField(component, member, value) : InteropStatus.UnsupportedType;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static InteropStatus InvokeAbi(ComponentHandle component, MemberHandle member, DynamicValueAbi* input, int count, DynamicValueAbi* output)
    {
        InteropStatus enter = EnterCall();
        if (enter != InteropStatus.Ok) return enter;
        try
        {
            if (count < 0 || count > 0 && input == null || output == null) return InteropStatus.InvalidArgument;
            object?[] arguments = new object?[count];
            for (int index = 0; index < count; ++index)
            {
                if (!InteropAbiConverter.TryDecode(input[index], out arguments[index])) return InteropStatus.UnsupportedType;
            }
            InteropStatus status = Invoke(component, member, arguments, out object? result);
            if (status != InteropStatus.Ok) return status;
            if (!TryResolveMember(component, member, InteropMemberKind.Method, out _, out MemberEntry? entry)) return InteropStatus.StaleHandle;
            return EncodeResult(result, output, entry.Method!.ReturnKind) ? InteropStatus.Ok : InteropStatus.UnsupportedType;
        }
        catch { return InteropStatus.InvocationFailed; }
        finally { ExitCall(); }
    }
}
