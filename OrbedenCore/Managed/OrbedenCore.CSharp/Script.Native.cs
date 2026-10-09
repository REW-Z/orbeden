using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
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

    //宿主函数表是否已绑定；诊断信息用它区分"脚本域没起来"与"宿主创建被拒"。
    internal static bool IsNativeApiBound => initialized;

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

        byte[] bytes = InteropText.EncodeUtf8(typeName);
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

    //从宿主字段表里删除一个字段，用于脚本类型不再声明它的时候。
    internal static bool RemoveHostField(IntPtr host, string name)
    {
        if (!initialized || host == IntPtr.Zero || api.RemoveField == null || string.IsNullOrEmpty(name))
            return false;

        byte[] nameBytes = InteropText.EncodeUtf8(name);
        fixed (byte* namePointer = nameBytes)
            return api.RemoveField(api.Context, host, namePointer, nameBytes.Length) != 0;
    }

    //在宿主字段表中新增或更新字段。
    internal static bool WriteHostField(IntPtr host, string name, string typeName, string value, bool inspectorVisible)
    {
        if (!initialized || host == IntPtr.Zero || api.SetField == null || string.IsNullOrEmpty(name))
            return false;

        byte[] nameBytes = InteropText.EncodeUtf8(name);
        byte[] typeBytes = InteropText.EncodeUtf8(typeName ?? string.Empty);
        byte[] valueBytes = InteropText.EncodeUtf8(value ?? string.Empty);
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
            return InteropText.DecodeUtf8(bytes, 0, actual);
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
            return InteropText.DecodeUtf8(bytes, 0, actual);
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
    internal static List<DroppedScriptField> InitializeEditorHost(IntPtr binding, IntPtr host, Ens ens, [DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicConstructors)] Type type)
    {
        if (binding == IntPtr.Zero || host == IntPtr.Zero || !NativeBindingRuntime.IsManagedScript(type))
            throw new InvalidOperationException("Invalid Editor script host.");
        //宿主已经由运行时持有实例（编辑态的脚本域给世界里每个宿主都建了 Wrapper）时，
        //不另造第二个实例——两阶段构造与生命周期都归运行时；也不能什么都不做：
        //那样这条补账路径会恒真空转，宿主字段表再也没人按类型补齐。
        //改用现有实例补缺失字段，类型对不上（换过类型/旧会话残留）就交给运行时去处理。
        if (Object.FindCachedObject(Object.GetInstanceId(host)) is Script live)
        {
            if (live.GetType() == type) ManagedTypeMetadataCache.EnsureHostFields(live, host);
            return [];
        }
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
            return ManagedTypeMetadataCache.SyncHostFields(script, host);
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
        byte[] keyBytes = InteropText.EncodeUtf8(key);
        byte[] typeBytes = InteropText.EncodeUtf8(type.FullName ?? type.Name);
        EnsId ens;
        int kind;
        IntPtr pointer;
        fixed (byte* keyPointer = keyBytes)
        fixed (byte* typePointer = typeBytes)
            pointer = api.ResolveReference(api.Context, keyPointer, keyBytes.Length,
                typePointer, typeBytes.Length, &ens, &kind);
        if (pointer == IntPtr.Zero) return null;
        int objectId = Object.GetInstanceId(pointer);
        //托管组件先按原生脚本宿主取已登记的包装，再校验声明的托管类型：
        //声明基类引用派生实例时按包装判定，不要求宿主类型名与声明完全相同。
        Object? value = NativeBindingRuntime.IsManagedScript(type)
            ? ScriptRuntime.GetOrCreateHost(pointer) ?? Object.FindCachedObject(objectId)
            : Object.FindCachedObject(objectId);
        value ??= NativeBindingRuntime.Wrap(objectId);
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
    public delegate* unmanaged[Cdecl]<void*, IntPtr, byte*, int, byte> RemoveField;
}
#pragma warning restore CS0649
