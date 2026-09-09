using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

public abstract unsafe partial class Script
{
    private sealed class ConstructionFrame
    {
        internal EnsId Ens;
        internal IntPtr Host;
        internal bool Consumed;
    }

    [ThreadStatic] private static Stack<ConstructionFrame>? constructionFrames;
    private static ScriptBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 Script 宿主函数表。
    internal static void InitializeNativeApi(ScriptBindApi value)
    {
        api = value;
        initialized = api.GetHostCount != null;
        if (!initialized) constructionFrames?.Clear();
    }

    //在调用用户脚本构造函数前建立线程局部原生宿主上下文。
    internal static IDisposable BeginConstruction(EnsId ens, IntPtr host)
    {
        if (!initialized || host == IntPtr.Zero)
            throw new InvalidOperationException("Script native host is unavailable.");

        ConstructionFrame frame = new() { Ens = ens, Host = host };
        (constructionFrames ??= new Stack<ConstructionFrame>()).Push(frame);
        return new ConstructionScope(frame);
    }

    //枚举当前 World 中按组件挂载顺序排列的全部托管脚本宿主。
    internal static IReadOnlyList<IntPtr> GetManagedHosts()
    {
        if (!initialized || api.GetHostCount == null || api.GetHostAt == null) return [];
        int count = Math.Max(0, api.GetHostCount(api.Context));
        List<IntPtr> hosts = new(count);
        for (int index = 0; index < count; ++index)
        {
            IntPtr host = api.GetHostAt(api.Context, index);
            if (host != IntPtr.Zero) hosts.Add(host);
        }
        return hosts;
    }

    //创建绑定到 Ens 的原生 Script 宿主。
    internal static IntPtr CreateManagedHost(EnsId ens, string typeName)
    {
        if (!initialized || api.CreateHost == null || ens.IsNull || string.IsNullOrWhiteSpace(typeName))
            return IntPtr.Zero;

        byte[] bytes = Encoding.UTF8.GetBytes(typeName);
        fixed (byte* pointer = bytes)
        {
            return api.CreateHost(api.Context, ens, pointer, bytes.Length);
        }
    }

    //移除原生 Script 宿主。
    internal static bool RemoveManagedHost(IntPtr host)
    {
        return initialized && host != IntPtr.Zero && api.RemoveHost != null
            && api.RemoveHost(api.Context, host) != 0;
    }

    //读取宿主所属 Ens。
    internal static EnsId GetHostEns(IntPtr host)
    {
        return initialized && host != IntPtr.Zero && api.GetEns != null
            ? api.GetEns(api.Context, host)
            : EnsId.Null;
    }

    //读取宿主声明的 C# 类型全名。
    internal static string GetHostTypeName(IntPtr host)
    {
        return ReadHostText(host, api.GetTypeName);
    }

    //读取宿主启用状态。
    internal static bool GetHostEnabled(IntPtr host)
    {
        return initialized && host != IntPtr.Zero && api.GetEnabled != null
            && api.GetEnabled(api.Context, host) != 0;
    }

    //写入宿主启用状态。

    //读取宿主保存的动态脚本字段。
    internal static IReadOnlyDictionary<string, ManagedHostField> ReadHostFields(IntPtr host)
    {
        Dictionary<string, ManagedHostField> fields = new(StringComparer.Ordinal);
        if (!initialized || host == IntPtr.Zero || api.GetFieldCount == null) return fields;

        int count = Math.Max(0, api.GetFieldCount(api.Context, host));
        for (int index = 0; index < count; ++index)
        {
            string name = ReadHostFieldText(host, index, api.GetFieldName);
            if (string.IsNullOrEmpty(name)) continue;
            fields[name] = new ManagedHostField(
                ReadHostFieldText(host, index, api.GetFieldTypeName),
                ReadHostFieldText(host, index, api.GetFieldValue));
        }
        return fields;
    }

    //在宿主字段表中新增或更新字段。
    internal static bool WriteHostField(IntPtr host, string name, string typeName, string value, bool inspectorVisible)
    {
        if (!initialized || host == IntPtr.Zero || api.SetField == null || string.IsNullOrEmpty(name))
            return false;

        byte[] nameBytes = Encoding.UTF8.GetBytes(name);
        byte[] typeBytes = Encoding.UTF8.GetBytes(typeName ?? string.Empty);
        byte[] valueBytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* namePointer = nameBytes)
        fixed (byte* typePointer = typeBytes)
        fixed (byte* valuePointer = valueBytes)
        {
            return api.SetField(api.Context, host,
                namePointer, nameBytes.Length,
                typePointer, typeBytes.Length,
                valuePointer, valueBytes.Length,
                inspectorVisible ? (byte)1 : (byte)0) != 0;
        }
    }

    //从最内层构造上下文取得唯一原生宿主。
    private static IntPtr ConsumeConstructionHost(Ens ens)
    {
        if (constructionFrames == null || constructionFrames.Count == 0)
            throw new InvalidOperationException(
                "C# Script cannot be created without a native Script host.");

        ConstructionFrame frame = constructionFrames.Peek();
        if (frame.Consumed || frame.Host == IntPtr.Zero || !frame.Ens.Equals(ens.Id))
            throw new InvalidOperationException(
                "C# Script construction context does not match its Ens.");

        frame.Consumed = true;
        return frame.Host;
    }

    //读取不带索引的 UTF-8 宿主字符串。
    private static string ReadHostText(IntPtr host,
        delegate* unmanaged[Cdecl]<void*, IntPtr, byte*, int, int> getter)
    {
        if (!initialized || host == IntPtr.Zero || getter == null) return string.Empty;
        int length = getter(api.Context, host, null, 0);
        if (length <= 0) return string.Empty;

        byte[] bytes = new byte[length];
        fixed (byte* pointer = bytes)
        {
            int actual = Math.Clamp(getter(api.Context, host, pointer, length), 0, length);
            return Encoding.UTF8.GetString(bytes, 0, actual);
        }
    }

    //读取带字段索引的 UTF-8 宿主字符串。
    private static string ReadHostFieldText(IntPtr host, int index,
        delegate* unmanaged[Cdecl]<void*, IntPtr, int, byte*, int, int> getter)
    {
        if (!initialized || host == IntPtr.Zero || getter == null) return string.Empty;
        int length = getter(api.Context, host, index, null, 0);
        if (length <= 0) return string.Empty;

        byte[] bytes = new byte[length];
        fixed (byte* pointer = bytes)
        {
            int actual = Math.Clamp(getter(api.Context, host, index, pointer, length), 0, length);
            return Encoding.UTF8.GetString(bytes, 0, actual);
        }
    }

    private sealed class ConstructionScope(ConstructionFrame frame) : IDisposable
    {
        private ConstructionFrame? activeFrame = frame;

        public void Dispose()
        {
            if (activeFrame == null) return;
            if (constructionFrames == null || constructionFrames.Count == 0
                || !ReferenceEquals(constructionFrames.Peek(), activeFrame))
                throw new InvalidOperationException(
                    "Script construction scopes must be disposed in stack order.");

            constructionFrames.Pop();
            activeFrame = null;
        }
    }

    /// <summary>临时使用 Editor 宿主表构造默认值，结束后释放反射 Wrapper。</summary>
    internal static void InitializeEditorHost(IntPtr binding, IntPtr host, Ens ens, Type type)
    {
        if (binding == IntPtr.Zero || host == IntPtr.Zero || !NativeBindingRuntime.IsManagedScript(type))
            throw new InvalidOperationException("Invalid Editor script host.");
        if (Object.FindCachedObject(Object.GetInstanceId(host)) is Script) return;
        ScriptBindApi previous = api;
        bool wasInitialized = initialized;
        Script? script = null;
        api = *(ScriptBindApi*)binding;
        initialized = true;
        try
        {
            using (BeginConstruction(ens.Id, host))
                script = type.GetConstructor([typeof(Ens)])?.Invoke([ens]) as Script;
            if (script == null) throw new InvalidOperationException("Script requires a public (Ens ens) constructor.");
            ManagedTypeMetadataCache.ApplyHostFields(script, host);
            ManagedTypeMetadataCache.WriteMissingHostFields(script, host);
        }
        finally
        {
            script?.DisconnectNative();
            ManagedTypeMetadataCache.Remove(type);
            api = previous;
            initialized = wasInitialized;
        }
    }

    /// <summary>按稳定路径恢复资源或组件引用。</summary>
    internal static Object? ResolveReference(string key, Type type)
    {
        if (!initialized || api.ResolveReference == null || string.IsNullOrEmpty(key)) return null;
        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        byte[] typeBytes = Encoding.UTF8.GetBytes(type.FullName ?? type.Name);
        EnsId ens;
        int kind;
        IntPtr pointer;
        fixed (byte* keyPointer = keyBytes)
        fixed (byte* typePointer = typeBytes)
            pointer = api.ResolveReference(api.Context, keyPointer, keyBytes.Length,
                typePointer, typeBytes.Length, &ens, &kind);
        if (pointer == IntPtr.Zero) return null;
        Object? value = Object.FindCachedObject(Object.GetInstanceId(pointer));
        if (value != null) return type.IsInstanceOfType(value) ? value : null;
        value = NativeBindingRuntime.Wrap(Object.GetInstanceId(pointer));
        return value != null && type.IsInstanceOfType(value) ? value : null;
    }
}

internal readonly record struct ManagedHostField(string TypeName, string Value);

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ScriptBindApi
{
    public void* Context;
    public delegate* unmanaged[Cdecl]<void*, int> GetHostCount;
    public delegate* unmanaged[Cdecl]<void*, int, IntPtr> GetHostAt;
    public delegate* unmanaged[Cdecl]<void*, EnsId, byte*, int, IntPtr> CreateHost;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, byte> RemoveHost;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, EnsId> GetEns;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, byte*, int, int> GetTypeName;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, byte> GetEnabled;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, byte, void> SetEnabled;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, int> GetFieldCount;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, int, byte*, int, int> GetFieldName;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, int, byte*, int, int> GetFieldTypeName;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, int, int> GetFieldKind;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, int, byte*, int, int> GetFieldValue;
    public delegate* unmanaged[Cdecl]<void*, IntPtr, byte*, int, byte*, int, byte*, int, byte, byte> SetField;
    public delegate* unmanaged[Cdecl]<void*, byte*, int, byte*, int, EnsId*, int*, IntPtr> ResolveReference;
}
#pragma warning restore CS0649
