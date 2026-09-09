using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

using System.Text;

namespace Orbeden;

/// <summary>原生对象托管包装基类。</summary>
public abstract partial class Object
{
    private sealed class WrapperEntry
    {
        public WeakReference<Object> wrapper = null!;
        public IntPtr nativePtr;
        public IntPtr handle;
    }

    private static readonly object cacheLock = new();
    private static readonly Dictionary<int, WrapperEntry> cache = [];

    /// <summary>通过原生反射工厂创建非组件对象。</summary>
    public static T? CreateInstance<T>() where T : Object => NativeBindingRuntime.Create(typeof(T)) as T;

    private IntPtr nativePtr;
    private int instanceId;
    internal uint BindingGeneration { get; private set; }

    /// <summary>创建空对象包装。</summary>
    protected Object() {}

    /// <summary>创建原生对象包装。</summary>
    protected Object(IntPtr pointer)
    {
        ConnectNative(pointer);
    }

    /// <summary>原生对象运行时 ID。</summary>
    public int InstanceId => instanceId;

    /// <summary>资源对象的稳定 Key；运行时临时对象也可能返回运行时路径。</summary>
    public string ResourceKey => Object.GetResourceKey(NativePtr);

    /// <summary>判断原生对象是否仍然存活。</summary>
    public bool IsAlive => instanceId != 0 && BindingGeneration == NativeBindingRuntime.Generation && Object.IsNativeAlive(instanceId);

    /// <summary>判断对象是否有效。</summary>
    public virtual bool IsValid => IsAlive;

    internal IntPtr NativePtr => IsAlive ? nativePtr : IntPtr.Zero;

    /// <summary>销毁原生对象。</summary>
    public static bool Destroy(Object? target)
    {
        if (target == null || !target.IsAlive) return false;

        bool destroyed = Object.Destroy(target.NativePtr);
        if (!target.IsAlive) target.DisconnectNative();
        return destroyed;
    }

    /// <summary>判断对象是否可用。</summary>
    public static implicit operator bool(Object? value)
    {
        return !(value == null);
    }

    public static bool operator ==(Object? lhs, Object? rhs)
    {
        bool lhsNull = ReferenceEquals(lhs, null) || !lhs.IsAlive;
        bool rhsNull = ReferenceEquals(rhs, null) || !rhs.IsAlive;
        if (lhsNull || rhsNull) return lhsNull == rhsNull;

        return lhs!.instanceId == rhs!.instanceId;
    }

    public static bool operator !=(Object? lhs, Object? rhs)
    {
        return !(lhs == rhs);
    }

    /// <summary>判断两个对象是否相同。</summary>
    public override bool Equals(object? obj)
    {
        return obj is Object other && this == other;
    }

    /// <summary>获取对象哈希值。</summary>
    public override int GetHashCode()
    {
        return instanceId;
    }

    /// <summary>返回对象调试文本。</summary>
    public override string ToString()
    {
        return $"{GetType().Name}({instanceId})";
    }

    //连接原生对象
    protected void ConnectNative(IntPtr pointer)
    {
        if (pointer == IntPtr.Zero) return;

        int id = Object.GetInstanceId(pointer);
        if (id == 0) return;

        nativePtr = pointer;
        instanceId = id;
        BindingGeneration = NativeBindingRuntime.Generation;

        lock (cacheLock)
        {
            if (cache.TryGetValue(id, out WrapperEntry? oldEntry) && oldEntry.handle != IntPtr.Zero)
            {
                Object.SetManagedWrapper(oldEntry.nativePtr, IntPtr.Zero);
                GCHandle.FromIntPtr(oldEntry.handle).Free();
            }

            GCHandle handle = GCHandle.Alloc(this, GCHandleType.Weak);
            IntPtr handlePtr = GCHandle.ToIntPtr(handle);
            cache[id] = new WrapperEntry
            {
                wrapper = new WeakReference<Object>(this),
                nativePtr = pointer,
                handle = handlePtr
            };

            Object.SetManagedWrapper(pointer, handlePtr);
        }
    }

    //断开原生对象
    internal void DisconnectNative()
    {
        if (instanceId == 0) return;

        lock (cacheLock)
        {
            if (cache.TryGetValue(instanceId, out WrapperEntry? entry)
                && entry.wrapper.TryGetTarget(out Object? target)
                && ReferenceEquals(target, this))
            {
                if (Object.IsNativeAlive(instanceId)) Object.SetManagedWrapper(entry.nativePtr, IntPtr.Zero);
                if (entry.handle != IntPtr.Zero) GCHandle.FromIntPtr(entry.handle).Free();
                cache.Remove(instanceId);
            }
        }

        nativePtr = IntPtr.Zero;
        instanceId = 0;
    }

    /// <summary>程序集卸载前断开其包装，释放原生弱句柄并允许新程序集重新包装。</summary>
    internal static void DisconnectAssembly(System.Reflection.Assembly assembly)
    {
        lock (cacheLock)
        {
            foreach (WrapperEntry entry in cache.Values.ToArray())
                if (entry.wrapper.TryGetTarget(out Object? target) && target.GetType().Assembly == assembly)
                    target.DisconnectNative();
        }
    }

    //按运行时 ID 获取已经存在的托管包装，不隐式猜测原生派生类型。
    internal static Object? FindCachedObject(int id)
    {
        if (id == 0) return null;
        lock (cacheLock)
        {
            return cache.TryGetValue(id, out WrapperEntry? entry)
                && entry.wrapper.TryGetTarget(out Object? target)
                && target.IsAlive
                ? target
                : null;
        }
    }

    /// <summary>按运行时 ID 返回已存在的托管包装，不创建未知原生派生类型。</summary>
    public static Object? FindLoadedObject(int id) => FindCachedObject(id);

    //收集当前活跃托管根
    internal static int[] CollectManagedRootIds()
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();

        List<int> roots = [];
        lock (cacheLock)
        {
            List<int> staleIds = [];
            foreach ((int id, WrapperEntry entry) in cache)
            {
                if (entry.wrapper.TryGetTarget(out Object? target) && target.IsAlive)
                {
                    roots.Add(id);
                    continue;
                }

                if (Object.IsNativeAlive(id)) Object.SetManagedWrapper(entry.nativePtr, IntPtr.Zero);
                if (entry.handle != IntPtr.Zero) GCHandle.FromIntPtr(entry.handle).Free();
                staleIds.Add(id);
            }

            foreach (int id in staleIds)
            {
                cache.Remove(id);
            }
        }

        return roots.ToArray();
    }
}

/// <summary>对象资源工具。</summary>
public static class Resources
{
    /// <summary>按原生类型加载资源并选择实际派生包装。</summary>
    public static T? Load<T>(string key) where T : Object => NativeBindingRuntime.Load<T>(key);
    /// <summary>释放未使用对象。</summary>
    public static uint UnloadUnusedObjects()
    {
        return Object.UnloadUnusedObjects(Object.CollectManagedRootIds());
    }
}

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ObjectBindApi
{
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetInstanceId;
    public delegate* unmanaged[Cdecl]<int, byte> IsAlive;
    public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> GetManagedWrapper;
    public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, void> SetManagedWrapper;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> Destroy;
    public delegate* unmanaged[Cdecl]<int*, int, uint> UnloadUnusedObjects;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ObjectExtensionBindApi
{
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetResourceKey;
}


#pragma warning restore CS0649
public abstract unsafe partial class Object
{
private static ObjectBindApi api;
    private static ObjectExtensionBindApi extensionApi;
    private static bool initialized;

    //保存 C++ 传入的 Object 函数表
    internal static void InitializeNativeApi(ObjectBindApi value, ObjectExtensionBindApi extensionValue)
    {
        api = value;
        extensionApi = extensionValue;
        initialized = api.GetInstanceId != null;
    }

    //读取原生对象 ID
    internal static int GetInstanceId(IntPtr pointer)
    {
        return initialized && pointer != IntPtr.Zero && api.GetInstanceId != null ? api.GetInstanceId(pointer) : 0;
    }

    //读取对象稳定资源 Key。
    internal static string GetResourceKey(IntPtr pointer)
    {
        if (!initialized || pointer == IntPtr.Zero || extensionApi.GetResourceKey == null) return string.Empty;

        int requiredBytes = extensionApi.GetResourceKey(pointer, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> bytes = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* output = bytes)
        {
            int actualBytes = extensionApi.GetResourceKey(pointer, output, requiredBytes);
            return Encoding.UTF8.GetString(bytes[..Math.Clamp(actualBytes, 0, requiredBytes)]);
        }
    }

    //判断原生对象是否存活
    internal static bool IsNativeAlive(int instanceId)
    {
        return initialized && instanceId != 0 && api.IsAlive != null && api.IsAlive(instanceId) != 0;
    }

    //读取托管包装缓存
    internal static IntPtr GetManagedWrapper(IntPtr pointer)
    {
        return initialized && pointer != IntPtr.Zero && api.GetManagedWrapper != null ? api.GetManagedWrapper(pointer) : IntPtr.Zero;
    }

    //写入托管包装缓存
    internal static void SetManagedWrapper(IntPtr pointer, IntPtr handle)
    {
        if (initialized && pointer != IntPtr.Zero && api.SetManagedWrapper != null) api.SetManagedWrapper(pointer, handle);
    }

    //销毁原生对象
    internal static bool Destroy(IntPtr pointer)
    {
        return initialized && pointer != IntPtr.Zero && api.Destroy != null && api.Destroy(pointer) != 0;
    }

    //释放未使用对象
    internal static uint UnloadUnusedObjects(int[] roots)
    {
        if (!initialized || api.UnloadUnusedObjects == null) return 0;

        fixed (int* rootPointer = roots)
        {
            return api.UnloadUnusedObjects(rootPointer, roots.Length);
        }
    }
}



