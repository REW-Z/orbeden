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

    private IntPtr nativePtr;
    private int instanceId;

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
    public bool IsAlive => instanceId != 0 && Object.IsNativeAlive(instanceId);

    /// <summary>判断对象是否有效。</summary>
    public virtual bool IsValid => IsAlive;

    internal IntPtr NativePtr => IsAlive ? nativePtr : IntPtr.Zero;

    /// <summary>销毁原生对象。</summary>
    public static bool Destroy(Object? target)
    {
        if (target == null || !target.IsAlive) return false;

        bool destroyed = Object.Destroy(target.NativePtr);
        target.DisconnectNative();
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
    private void DisconnectNative()
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

    //从原生指针获取托管包装
    internal static T? FromNative<T>(IntPtr pointer, Func<IntPtr, T> create) where T : Object
    {
        if (pointer == IntPtr.Zero) return null;

        IntPtr handle = Object.GetManagedWrapper(pointer);
        if (handle != IntPtr.Zero && GCHandle.FromIntPtr(handle).Target is T cached && cached.IsAlive)
        {
            return cached;
        }

        int id = Object.GetInstanceId(pointer);
        if (id == 0) return null;

        lock (cacheLock)
        {
            if (cache.TryGetValue(id, out WrapperEntry? entry)
                && entry.wrapper.TryGetTarget(out Object? target)
                && target is T typed
                && typed.IsAlive)
            {
                return typed;
            }
        }

        return create(pointer);
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
    /// <summary>释放未使用对象。</summary>
    public static uint UnloadUnusedObjects()
    {
        return Object.UnloadUnusedObjects(Object.CollectManagedRootIds());
    }
}

/// <summary>Mesh 子网格信息。</summary>
public readonly struct SubMeshInfo
{
    public readonly string name;
    public readonly uint indexStart;
    public readonly uint indexCount;
    public readonly Material? material;

    /// <summary>创建 Mesh 子网格信息。</summary>
    public SubMeshInfo(string name, uint indexStart, uint indexCount, Material? material)
    {
        this.name = name;
        this.indexStart = indexStart;
        this.indexCount = indexCount;
        this.material = material;
    }
}

/// <summary>CPU Mesh 资源托管包装。</summary>
public sealed partial class Mesh : Object
{
    /// <summary>创建运行时 Mesh。</summary>
    public Mesh() : this("Mesh") {}

    /// <summary>创建运行时 Mesh。</summary>
    public Mesh(string name)
    {
        ConnectNative(Mesh.Create(name));
    }

    private Mesh(IntPtr pointer) : base(pointer) {}

    //从原生指针获取 Mesh 包装
    internal static Mesh? FromNative(IntPtr pointer)
    {
        return Object.FromNative(pointer, value => new Mesh(value));
    }

    /// <summary>加载 Mesh 资源。</summary>
    public static Mesh? Load(string key)
    {
        return FromNative(Mesh.LoadNative(key));
    }

    /// <summary>判断资源是否已加载且类型正确。</summary>
    public override bool IsValid => Mesh.IsNativeValid(NativePtr);

    /// <summary>Mesh 名称。</summary>
    public string name
    {
        get => Mesh.GetName(NativePtr);
        set => Mesh.SetName(NativePtr, value);
    }

    /// <summary>Mesh 是否需要刷新 GPU 数据。</summary>
    public bool dirty => Mesh.IsDirty(NativePtr);

    /// <summary>标记所有 Mesh 数据已修改。</summary>
    public void MarkDirty() => Mesh.MarkDirty(NativePtr);

    /// <summary>顶点数量。</summary>
    public int vertexCount => Mesh.GetVertexCount(NativePtr);

    /// <summary>索引数量。</summary>
    public int indexCount => Mesh.GetIndexCount(NativePtr);

    /// <summary>子网格数量。</summary>
    public int subMeshCount => Mesh.GetSubMeshCount(NativePtr);

    /// <summary>顶点位置数组副本。</summary>
    public vector3[] vertexPositions
    {
        get => Mesh.GetVertexPositions(NativePtr);
        set => SetVertexPositions(value);
    }

    /// <summary>顶点法线数组副本。</summary>
    public vector3[] vertexNormals
    {
        get => Mesh.GetVertexNormals(NativePtr);
        set => SetVertexNormals(value);
    }

    /// <summary>顶点 UV 数组副本。</summary>
    public vector2[] vertexTexcoords
    {
        get => Mesh.GetVertexTexcoords(NativePtr);
        set => SetVertexTexcoords(value);
    }

    /// <summary>顶点切线数组副本。</summary>
    public vector3[] vertexTangents
    {
        get => Mesh.GetVertexTangents(NativePtr);
        set => SetVertexTangents(value);
    }

    /// <summary>索引数组副本。</summary>
    public uint[] indexData
    {
        get => Mesh.GetIndexData(NativePtr);
        set => SetIndexData(value);
    }

    /// <summary>读取子网格信息。</summary>
    public SubMeshInfo GetSubMesh(int index)
    {
        return new SubMeshInfo(
            Mesh.GetSubMeshName(NativePtr, index),
            Mesh.GetSubMeshIndexStart(NativePtr, index),
            Mesh.GetSubMeshIndexCount(NativePtr, index),
            Material.FromNative(Mesh.GetSubMeshMaterial(NativePtr, index)));
    }

    /// <summary>写入顶点位置数组。</summary>
    public bool SetVertexPositions(vector3[]? values) => Mesh.SetVertexPositions(NativePtr, values);

    /// <summary>写入顶点法线数组。</summary>
    public bool SetVertexNormals(vector3[]? values) => Mesh.SetVertexNormals(NativePtr, values);

    /// <summary>写入顶点 UV 数组。</summary>
    public bool SetVertexTexcoords(vector2[]? values) => Mesh.SetVertexTexcoords(NativePtr, values);

    /// <summary>写入顶点切线数组。</summary>
    public bool SetVertexTangents(vector3[]? values) => Mesh.SetVertexTangents(NativePtr, values);

    /// <summary>写入索引数组。</summary>
    public bool SetIndexData(uint[]? values) => Mesh.SetIndexData(NativePtr, values);

    /// <summary>清空几何数据。</summary>
    public bool ClearGeometry() => Mesh.ClearGeometry(NativePtr);

    /// <summary>根据三角形索引重新计算法线。</summary>
    public bool RefreshNormals() => Mesh.RefreshNormals(NativePtr);

    /// <summary>调整子网格数量。</summary>
    public bool ResizeSubMeshes(int count) => Mesh.ResizeSubMeshes(NativePtr, count);

    /// <summary>配置子网格。</summary>
    public bool ConfigureSubMesh(int index, string name, uint indexStart, uint indexCount, Material? material)
    {
        return Mesh.ConfigureSubMesh(NativePtr, index, name, indexStart, indexCount, material?.NativePtr ?? IntPtr.Zero);
    }
}

/// <summary>CPU Material 资源托管包装。</summary>
public sealed partial class Material : Object
{
    /// <summary>创建运行时 Material。</summary>
    public Material(Shader? shader) : this(shader, "Material") {}

    /// <summary>创建运行时 Material。</summary>
    public Material(Shader? shader, string name)
    {
        ConnectNative(Material.Create(name, shader?.NativePtr ?? IntPtr.Zero));
    }

    private Material(IntPtr pointer) : base(pointer) {}

    //从原生指针获取 Material 包装
    internal static Material? FromNative(IntPtr pointer)
    {
        return Object.FromNative(pointer, value => new Material(value));
    }

    /// <summary>加载 Material 资源。</summary>
    public static Material? Load(string key)
    {
        return FromNative(Material.LoadNative(key));
    }

    /// <summary>判断资源是否已加载且类型正确。</summary>
    public override bool IsValid => Material.IsNativeValid(NativePtr);

    /// <summary>Material 名称。</summary>
    public string name => Material.GetName(NativePtr);

    /// <summary>材质是否需要刷新 GPU 数据。</summary>
    public bool dirty => Material.IsDirty(NativePtr);

    /// <summary>标记材质需要刷新 GPU 数据。</summary>
    public void MarkDirty() => Material.MarkDirty(NativePtr);

    /// <summary>材质使用的 Shader。</summary>
    public Shader? shader
    {
        get => Shader.FromNative(Material.GetShader(NativePtr));
        set => Material.SetShader(NativePtr, value?.NativePtr ?? IntPtr.Zero);
    }

    /// <summary>判断纹理槽是否存在。</summary>
    public bool HasTexture(string slotName) => Material.HasTexture(NativePtr, slotName);

    /// <summary>读取纹理资源 Key。</summary>
    public string GetTextureKey(string slotName) => Material.GetTexture(NativePtr, slotName);

    /// <summary>读取纹理资源 Key。</summary>
    public string GetTextureResource(string slotName) => GetTextureKey(slotName);

    /// <summary>写入纹理资源 Key。</summary>
    public bool SetTextureKey(string slotName, string textureKey) => Material.SetTexture(NativePtr, slotName, textureKey);

    /// <summary>写入纹理资源 Key。</summary>
    public bool SetTextureResource(string slotName, string textureKey) => SetTextureKey(slotName, textureKey);

    /// <summary>清除纹理槽。</summary>
    public bool ClearTexture(string slotName) => Material.ClearTexture(NativePtr, slotName);

    /// <summary>判断颜色槽是否存在。</summary>
    public bool HasColor(string slotName) => Material.HasColor(NativePtr, slotName);

    /// <summary>读取颜色槽。</summary>
    public color4 GetColor(string slotName) => GetColor(slotName, new color4(0.0f, 0.0f, 0.0f, 1.0f));

    /// <summary>读取颜色槽。</summary>
    public color4 GetColor(string slotName, color4 defaultValue) => Material.GetColor(NativePtr, slotName, defaultValue);

    /// <summary>读取颜色槽。</summary>
    public color4 GetColorValue(string slotName) => GetColor(slotName);

    /// <summary>读取颜色槽。</summary>
    public color4 GetColorValue(string slotName, color4 defaultValue) => GetColor(slotName, defaultValue);

    /// <summary>写入颜色槽。</summary>
    public bool SetColor(string slotName, color4 value) => Material.SetColor(NativePtr, slotName, value);

    /// <summary>写入颜色槽。</summary>
    public bool SetColorValue(string slotName, color4 value) => SetColor(slotName, value);

    /// <summary>清除颜色槽。</summary>
    public bool ClearColor(string slotName) => Material.ClearColor(NativePtr, slotName);

    /// <summary>判断浮点槽是否存在。</summary>
    public bool HasFloat(string slotName) => Material.HasFloat(NativePtr, slotName);

    /// <summary>读取浮点槽。</summary>
    public float GetFloat(string slotName) => GetFloat(slotName, 0.0f);

    /// <summary>读取浮点槽。</summary>
    public float GetFloat(string slotName, float defaultValue) => Material.GetFloat(NativePtr, slotName, defaultValue);

    /// <summary>读取浮点槽。</summary>
    public float GetFloatValue(string slotName) => GetFloat(slotName);

    /// <summary>读取浮点槽。</summary>
    public float GetFloatValue(string slotName, float defaultValue) => GetFloat(slotName, defaultValue);

    /// <summary>写入浮点槽。</summary>
    public bool SetFloat(string slotName, float value) => Material.SetFloat(NativePtr, slotName, value);

    /// <summary>写入浮点槽。</summary>
    public bool SetFloatValue(string slotName, float value) => SetFloat(slotName, value);

    /// <summary>清除浮点槽。</summary>
    public bool ClearFloat(string slotName) => Material.ClearFloat(NativePtr, slotName);
}

/// <summary>Shader 纹理槽维度。</summary>
public enum ShaderTextureDimension
{
    Texture2D = 0,
}

/// <summary>Shader 纹理槽信息。</summary>
public readonly struct ShaderTextureSlotInfo
{
    public readonly string name;
    public readonly string displayName;
    public readonly ShaderTextureDimension dimension;

    /// <summary>创建 Shader 纹理槽信息。</summary>
    public ShaderTextureSlotInfo(string name, string displayName, ShaderTextureDimension dimension)
    {
        this.name = name;
        this.displayName = displayName;
        this.dimension = dimension;
    }
}

/// <summary>Shader 颜色槽信息。</summary>
public readonly struct ShaderColorSlotInfo
{
    public readonly string name;
    public readonly string displayName;
    public readonly color4 defaultValue;

    /// <summary>创建 Shader 颜色槽信息。</summary>
    public ShaderColorSlotInfo(string name, string displayName, color4 defaultValue)
    {
        this.name = name;
        this.displayName = displayName;
        this.defaultValue = defaultValue;
    }
}

/// <summary>Shader 浮点槽信息。</summary>
public readonly struct ShaderFloatSlotInfo
{
    public readonly string name;
    public readonly string displayName;
    public readonly float defaultValue;

    /// <summary>创建 Shader 浮点槽信息。</summary>
    public ShaderFloatSlotInfo(string name, string displayName, float defaultValue)
    {
        this.name = name;
        this.displayName = displayName;
        this.defaultValue = defaultValue;
    }
}

/// <summary>Shader Pass 布尔状态。</summary>
public enum ShaderPassToggle
{
    Auto,
    On,
    Off,
}

/// <summary>Shader Pass 三角形剔除模式。</summary>
public enum ShaderCullMode
{
    Auto,
    None,
    Front,
    Back,
}

/// <summary>Shader Pass 只读信息。</summary>
public readonly struct ShaderPassInfo
{
    public readonly string name;
    public readonly ShaderPassToggle depthTest;
    public readonly ShaderPassToggle depthWrite;
    public readonly ShaderPassToggle blend;
    public readonly ShaderCullMode cull;

    /// <summary>创建 Shader Pass 信息。</summary>
    public ShaderPassInfo(string name, ShaderPassToggle depthTest, ShaderPassToggle depthWrite,
        ShaderPassToggle blend, ShaderCullMode cull)
    {
        this.name = name;
        this.depthTest = depthTest;
        this.depthWrite = depthWrite;
        this.blend = blend;
        this.cull = cull;
    }
}

/// <summary>CPU Shader 资源托管包装。</summary>
public sealed partial class Shader : Object
{
    private Shader(IntPtr pointer) : base(pointer) {}

    //从原生指针获取 Shader 包装
    internal static Shader? FromNative(IntPtr pointer)
    {
        return Object.FromNative(pointer, value => new Shader(value));
    }

    /// <summary>加载 Shader 资源。</summary>
    public static Shader? Load(string key)
    {
        return FromNative(Shader.LoadNative(key));
    }

    /// <summary>从 GLSL 源码创建运行时 Shader。</summary>
    public static Shader? CreateFromSource(string name, string vertexSource, string fragmentSource)
    {
        return FromNative(Shader.CreateFromSourceNative(name, vertexSource, fragmentSource));
    }

    /// <summary>判断资源是否已加载且类型正确。</summary>
    public override bool IsValid => Shader.IsNativeValid(NativePtr);

    /// <summary>Shader 名称。</summary>
    public string name => Shader.GetName(NativePtr);

    /// <summary>Shader 是否需要刷新 GPU 数据。</summary>
    public bool dirty => Shader.IsDirty(NativePtr);

    /// <summary>标记 Shader 需要刷新 GPU 数据。</summary>
    public void MarkDirty() => Shader.MarkDirty(NativePtr);

    /// <summary>顶点源码路径。</summary>
    public string vertexPath => Shader.GetVertexPath(NativePtr);

    /// <summary>片元源码路径。</summary>
    public string fragmentPath => Shader.GetFragmentPath(NativePtr);

    /// <summary>纹理槽数量。</summary>
    public int textureSlotCount => Shader.GetTextureSlotCount(NativePtr);

    /// <summary>颜色槽数量。</summary>
    public int colorSlotCount => Shader.GetColorSlotCount(NativePtr);

    /// <summary>浮点槽数量。</summary>
    public int floatSlotCount => Shader.GetFloatSlotCount(NativePtr);

    /// <summary>Pass 数量。</summary>
    public int passCount => Shader.GetPassCount(NativePtr);

    /// <summary>替换 GLSL 源码并刷新材质槽反射结果。</summary>
    public bool ReplaceSource(string vertexSource, string fragmentSource)
    {
        return Shader.ReplaceSource(NativePtr, vertexSource, fragmentSource);
    }

    /// <summary>读取指定 Pass 的只读状态。</summary>
    public ShaderPassInfo GetPass(int index)
    {
        return new ShaderPassInfo(
            Shader.GetPassName(NativePtr, index),
            (ShaderPassToggle)Shader.GetPassDepthTest(NativePtr, index),
            (ShaderPassToggle)Shader.GetPassDepthWrite(NativePtr, index),
            (ShaderPassToggle)Shader.GetPassBlend(NativePtr, index),
            (ShaderCullMode)Shader.GetPassCull(NativePtr, index));
    }

    /// <summary>读取纹理槽信息。</summary>
    public ShaderTextureSlotInfo GetTextureSlot(int index)
    {
        return new ShaderTextureSlotInfo(
            Shader.GetTextureSlotName(NativePtr, index),
            Shader.GetTextureSlotDisplayName(NativePtr, index),
            (ShaderTextureDimension)Shader.GetTextureSlotDimension(NativePtr, index));
    }

    /// <summary>读取颜色槽信息。</summary>
    public ShaderColorSlotInfo GetColorSlot(int index)
    {
        return new ShaderColorSlotInfo(
            Shader.GetColorSlotName(NativePtr, index),
            Shader.GetColorSlotDisplayName(NativePtr, index),
            Shader.GetColorSlotDefault(NativePtr, index));
    }

    /// <summary>读取浮点槽信息。</summary>
    public ShaderFloatSlotInfo GetFloatSlot(int index)
    {
        return new ShaderFloatSlotInfo(
            Shader.GetFloatSlotName(NativePtr, index),
            Shader.GetFloatSlotDisplayName(NativePtr, index),
            Shader.GetFloatSlotDefault(NativePtr, index));
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

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct MeshBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, IntPtr> Load;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsValid;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetVertexCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetIndexCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetSubMeshCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetSubMeshName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, uint> GetSubMeshIndexStart;
    public delegate* unmanaged[Cdecl]<IntPtr, int, uint> GetSubMeshIndexCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, IntPtr> GetSubMeshMaterial;
    public delegate* unmanaged[Cdecl]<byte*, int, IntPtr> CreateInstance;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> SetName;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsDirty;
    public delegate* unmanaged[Cdecl]<IntPtr, void> MarkDirty;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, int> GetVertexPositions;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, byte> SetVertexPositions;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, int> GetVertexNormals;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, byte> SetVertexNormals;
    public delegate* unmanaged[Cdecl]<IntPtr, vector2*, int, int> GetVertexTexcoords;
    public delegate* unmanaged[Cdecl]<IntPtr, vector2*, int, byte> SetVertexTexcoords;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, int> GetVertexTangents;
    public delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, byte> SetVertexTangents;
    public delegate* unmanaged[Cdecl]<IntPtr, uint*, int, int> GetIndexData;
    public delegate* unmanaged[Cdecl]<IntPtr, uint*, int, byte> SetIndexData;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> ClearGeometry;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> RefreshNormals;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte> ResizeSubMeshes;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, uint, uint, IntPtr, byte> ConfigureSubMesh;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct MaterialBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, IntPtr> Load;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsValid;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<IntPtr, IntPtr> GetShader;
    public delegate* unmanaged[Cdecl]<IntPtr, IntPtr, byte> SetShader;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> HasTexture;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, int> GetTexture;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, byte> SetTexture;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> ClearTexture;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> HasColor;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, color4, color4> GetColor;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, color4, byte> SetColor;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> ClearColor;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> HasFloat;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, float, float> GetFloat;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, float, byte> SetFloat;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> ClearFloat;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsDirty;
    public delegate* unmanaged[Cdecl]<IntPtr, void> MarkDirty;
    public delegate* unmanaged[Cdecl]<byte*, int, IntPtr, IntPtr> CreateInstance;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct ShaderBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, IntPtr> Load;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsValid;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetVertexPath;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> GetFragmentPath;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetTextureSlotCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetTextureSlotName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetTextureSlotDisplayName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetTextureSlotDimension;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetColorSlotCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetColorSlotName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetColorSlotDisplayName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, color4> GetColorSlotDefault;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetFloatSlotCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetFloatSlotName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetFloatSlotDisplayName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, float> GetFloatSlotDefault;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetPassCount;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetPassName;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetPassDepthTest;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetPassDepthWrite;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetPassBlend;
    public delegate* unmanaged[Cdecl]<IntPtr, int, int> GetPassCull;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, byte*, int, IntPtr> CreateFromSource;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, byte> ReplaceSource;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsDirty;
    public delegate* unmanaged[Cdecl]<IntPtr, void> MarkDirty;
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



public sealed unsafe partial class Mesh
{
private static MeshBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 Mesh 函数表
    internal static void InitializeNativeApi(MeshBindApi value)
    {
        api = value;
        initialized = api.Load != null;
    }

    //加载 Mesh 资源
    internal static IntPtr LoadNative(string key)
    {
        return initialized ? CallString(api.Load, key) : IntPtr.Zero;
    }

    //创建运行时 Mesh
    internal static IntPtr Create(string name)
    {
        return initialized ? CallString(api.CreateInstance, name) : IntPtr.Zero;
    }

    //判断 Mesh 是否有效
    internal static bool IsNativeValid(IntPtr mesh)
    {
        return initialized && mesh != IntPtr.Zero && api.IsValid != null && api.IsValid(mesh) != 0;
    }

    //读取 Mesh 名称
    internal static string GetName(IntPtr mesh)
    {
        return initialized ? GetString(api.GetName, mesh) : string.Empty;
    }

    //写入 Mesh 名称
    internal static bool SetName(IntPtr mesh, string name)
    {
        if (!initialized || mesh == IntPtr.Zero || api.SetName == null) return false;

        byte[] bytes = Encoding.UTF8.GetBytes(name ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return api.SetName(mesh, pointer, bytes.Length) != 0;
        }
    }

    //判断 Mesh 是否需要刷新 GPU 数据
    internal static bool IsDirty(IntPtr mesh)
    {
        return initialized && mesh != IntPtr.Zero && api.IsDirty != null && api.IsDirty(mesh) != 0;
    }

    //标记 Mesh 所有数据已修改
    internal static void MarkDirty(IntPtr mesh)
    {
        if (initialized && mesh != IntPtr.Zero && api.MarkDirty != null) api.MarkDirty(mesh);
    }

    //读取顶点数量
    internal static int GetVertexCount(IntPtr mesh) => initialized && mesh != IntPtr.Zero ? api.GetVertexCount(mesh) : 0;

    //读取索引数量
    internal static int GetIndexCount(IntPtr mesh) => initialized && mesh != IntPtr.Zero ? api.GetIndexCount(mesh) : 0;

    //读取子网格数量
    internal static int GetSubMeshCount(IntPtr mesh) => initialized && mesh != IntPtr.Zero ? api.GetSubMeshCount(mesh) : 0;

    //读取子网格名称
    internal static string GetSubMeshName(IntPtr mesh, int index) => initialized ? GetString(api.GetSubMeshName, mesh, index) : string.Empty;

    //读取子网格起始索引
    internal static uint GetSubMeshIndexStart(IntPtr mesh, int index) => initialized && mesh != IntPtr.Zero ? api.GetSubMeshIndexStart(mesh, index) : 0;

    //读取子网格索引数量
    internal static uint GetSubMeshIndexCount(IntPtr mesh, int index) => initialized && mesh != IntPtr.Zero ? api.GetSubMeshIndexCount(mesh, index) : 0;

    //读取子网格材质
    internal static IntPtr GetSubMeshMaterial(IntPtr mesh, int index)
    {
        return initialized && mesh != IntPtr.Zero && api.GetSubMeshMaterial != null ? api.GetSubMeshMaterial(mesh, index) : IntPtr.Zero;
    }

    //读取顶点位置数组
    internal static vector3[] GetVertexPositions(IntPtr mesh) => GetVector3Array(api.GetVertexPositions, mesh);

    //读取顶点法线数组
    internal static vector3[] GetVertexNormals(IntPtr mesh) => GetVector3Array(api.GetVertexNormals, mesh);

    //读取顶点 UV 数组
    internal static vector2[] GetVertexTexcoords(IntPtr mesh) => GetVector2Array(api.GetVertexTexcoords, mesh);

    //读取顶点切线数组
    internal static vector3[] GetVertexTangents(IntPtr mesh) => GetVector3Array(api.GetVertexTangents, mesh);

    //读取索引数组
    internal static uint[] GetIndexData(IntPtr mesh) => GetUIntArray(api.GetIndexData, mesh);

    //写入顶点位置数组
    internal static bool SetVertexPositions(IntPtr mesh, vector3[]? values) => SetVector3Array(api.SetVertexPositions, mesh, values);

    //写入顶点法线数组
    internal static bool SetVertexNormals(IntPtr mesh, vector3[]? values) => SetVector3Array(api.SetVertexNormals, mesh, values);

    //写入顶点 UV 数组
    internal static bool SetVertexTexcoords(IntPtr mesh, vector2[]? values) => SetVector2Array(api.SetVertexTexcoords, mesh, values);

    //写入顶点切线数组
    internal static bool SetVertexTangents(IntPtr mesh, vector3[]? values) => SetVector3Array(api.SetVertexTangents, mesh, values);

    //写入索引数组
    internal static bool SetIndexData(IntPtr mesh, uint[]? values) => SetUIntArray(api.SetIndexData, mesh, values);

    //清空几何数据
    internal static bool ClearGeometry(IntPtr mesh) => initialized && mesh != IntPtr.Zero && api.ClearGeometry(mesh) != 0;

    //重新计算法线
    internal static bool RefreshNormals(IntPtr mesh) => initialized && mesh != IntPtr.Zero && api.RefreshNormals(mesh) != 0;

    //调整子网格数量
    internal static bool ResizeSubMeshes(IntPtr mesh, int count) => initialized && mesh != IntPtr.Zero && api.ResizeSubMeshes(mesh, count) != 0;

    //配置子网格
    internal static bool ConfigureSubMesh(IntPtr mesh, int index, string name, uint indexStart, uint indexCount, IntPtr material)
    {
        if (!initialized || mesh == IntPtr.Zero || api.ConfigureSubMesh == null) return false;

        byte[] nameBytes = Encoding.UTF8.GetBytes(name ?? string.Empty);
        fixed (byte* namePointer = nameBytes)
        {
            return api.ConfigureSubMesh(mesh, index, namePointer, nameBytes.Length, indexStart, indexCount, material) != 0;
        }
    }

    //调用字符串输入并返回指针
    private static IntPtr CallString(delegate* unmanaged[Cdecl]<byte*, int, IntPtr> function, string? value)
    {
        if (function == null) return IntPtr.Zero;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length);
        }
    }

    //读取对象字符串
    private static string GetString(delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> function, IntPtr pointer)
    {
        if (function == null || pointer == IntPtr.Zero) return string.Empty;

        int requiredBytes = function(pointer, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = function(pointer, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return Encoding.UTF8.GetString(output[..length]);
        }
    }

    //读取对象索引字符串
    private static string GetString(delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> function, IntPtr pointer, int index)
    {
        if (function == null || pointer == IntPtr.Zero) return string.Empty;

        int requiredBytes = function(pointer, index, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = function(pointer, index, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return Encoding.UTF8.GetString(output[..length]);
        }
    }

    //读取 vector3 数组
    private static vector3[] GetVector3Array(delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, int> function, IntPtr mesh)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return [];

        int count = function(mesh, null, 0);
        if (count <= 0) return [];

        vector3[] values = new vector3[count];
        fixed (vector3* pointer = values)
        {
            int actualCount = function(mesh, pointer, values.Length);
            if (actualCount == values.Length) return values;
            Array.Resize(ref values, Math.Clamp(actualCount, 0, values.Length));
            return values;
        }
    }

    //读取 vector2 数组
    private static vector2[] GetVector2Array(delegate* unmanaged[Cdecl]<IntPtr, vector2*, int, int> function, IntPtr mesh)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return [];

        int count = function(mesh, null, 0);
        if (count <= 0) return [];

        vector2[] values = new vector2[count];
        fixed (vector2* pointer = values)
        {
            int actualCount = function(mesh, pointer, values.Length);
            if (actualCount == values.Length) return values;
            Array.Resize(ref values, Math.Clamp(actualCount, 0, values.Length));
            return values;
        }
    }

    //读取 uint 数组
    private static uint[] GetUIntArray(delegate* unmanaged[Cdecl]<IntPtr, uint*, int, int> function, IntPtr mesh)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return [];

        int count = function(mesh, null, 0);
        if (count <= 0) return [];

        uint[] values = new uint[count];
        fixed (uint* pointer = values)
        {
            int actualCount = function(mesh, pointer, values.Length);
            if (actualCount == values.Length) return values;
            Array.Resize(ref values, Math.Clamp(actualCount, 0, values.Length));
            return values;
        }
    }

    //写入 vector3 数组
    private static bool SetVector3Array(delegate* unmanaged[Cdecl]<IntPtr, vector3*, int, byte> function, IntPtr mesh, vector3[]? values)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return false;

        fixed (vector3* pointer = values)
        {
            return function(mesh, pointer, values?.Length ?? 0) != 0;
        }
    }

    //写入 vector2 数组
    private static bool SetVector2Array(delegate* unmanaged[Cdecl]<IntPtr, vector2*, int, byte> function, IntPtr mesh, vector2[]? values)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return false;

        fixed (vector2* pointer = values)
        {
            return function(mesh, pointer, values?.Length ?? 0) != 0;
        }
    }

    //写入 uint 数组
    private static bool SetUIntArray(delegate* unmanaged[Cdecl]<IntPtr, uint*, int, byte> function, IntPtr mesh, uint[]? values)
    {
        if (!initialized || mesh == IntPtr.Zero || function == null) return false;

        fixed (uint* pointer = values)
        {
            return function(mesh, pointer, values?.Length ?? 0) != 0;
        }
    }
}



public sealed unsafe partial class Material
{
private static MaterialBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 Material 函数表
    internal static void InitializeNativeApi(MaterialBindApi value)
    {
        api = value;
        initialized = api.Load != null;
    }

    //加载 Material 资源
    internal static IntPtr LoadNative(string key) => initialized ? CallString(api.Load, key) : IntPtr.Zero;

    //创建运行时 Material
    internal static IntPtr Create(string name, IntPtr shader) => initialized ? CallStringPointer(api.CreateInstance, name, shader) : IntPtr.Zero;

    //判断 Material 是否有效
    internal static bool IsNativeValid(IntPtr material) => initialized && material != IntPtr.Zero && api.IsValid(material) != 0;

    //读取 Material 名称
    internal static string GetName(IntPtr material) => initialized ? GetString(api.GetName, material) : string.Empty;

    //判断材质是否需要刷新 GPU 数据
    internal static bool IsDirty(IntPtr material) => initialized && material != IntPtr.Zero && api.IsDirty != null && api.IsDirty(material) != 0;

    //标记材质需要刷新 GPU 数据
    internal static void MarkDirty(IntPtr material)
    {
        if (initialized && material != IntPtr.Zero && api.MarkDirty != null) api.MarkDirty(material);
    }

    //读取材质 Shader
    internal static IntPtr GetShader(IntPtr material) => initialized && material != IntPtr.Zero ? api.GetShader(material) : IntPtr.Zero;

    //设置材质 Shader
    internal static bool SetShader(IntPtr material, IntPtr shader) => initialized && material != IntPtr.Zero && api.SetShader(material, shader) != 0;

    //判断纹理槽是否存在
    internal static bool HasTexture(IntPtr material, string slotName) => CallSlotBool(api.HasTexture, material, slotName);

    //读取纹理资源 Key
    internal static string GetTexture(IntPtr material, string slotName) => GetSlotString(api.GetTexture, material, slotName);

    //写入纹理资源 Key
    internal static bool SetTexture(IntPtr material, string slotName, string textureKey)
    {
        if (!initialized || material == IntPtr.Zero || api.SetTexture == null) return false;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        byte[] keyBytes = Encoding.UTF8.GetBytes(textureKey ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        fixed (byte* keyPointer = keyBytes)
        {
            return api.SetTexture(material, slotPointer, slotBytes.Length, keyPointer, keyBytes.Length) != 0;
        }
    }

    //清除纹理槽
    internal static bool ClearTexture(IntPtr material, string slotName) => CallSlotBool(api.ClearTexture, material, slotName);

    //判断颜色槽是否存在
    internal static bool HasColor(IntPtr material, string slotName) => CallSlotBool(api.HasColor, material, slotName);

    //读取颜色槽
    internal static color4 GetColor(IntPtr material, string slotName, color4 defaultValue)
    {
        if (!initialized || material == IntPtr.Zero || api.GetColor == null) return defaultValue;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            return api.GetColor(material, slotPointer, slotBytes.Length, defaultValue);
        }
    }

    //写入颜色槽
    internal static bool SetColor(IntPtr material, string slotName, color4 value)
    {
        if (!initialized || material == IntPtr.Zero || api.SetColor == null) return false;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            return api.SetColor(material, slotPointer, slotBytes.Length, value) != 0;
        }
    }

    //清除颜色槽
    internal static bool ClearColor(IntPtr material, string slotName) => CallSlotBool(api.ClearColor, material, slotName);

    //判断浮点槽是否存在
    internal static bool HasFloat(IntPtr material, string slotName) => CallSlotBool(api.HasFloat, material, slotName);

    //读取浮点槽
    internal static float GetFloat(IntPtr material, string slotName, float defaultValue)
    {
        if (!initialized || material == IntPtr.Zero || api.GetFloat == null) return defaultValue;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            return api.GetFloat(material, slotPointer, slotBytes.Length, defaultValue);
        }
    }

    //写入浮点槽
    internal static bool SetFloat(IntPtr material, string slotName, float value)
    {
        if (!initialized || material == IntPtr.Zero || api.SetFloat == null) return false;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            return api.SetFloat(material, slotPointer, slotBytes.Length, value) != 0;
        }
    }

    //清除浮点槽
    internal static bool ClearFloat(IntPtr material, string slotName) => CallSlotBool(api.ClearFloat, material, slotName);

    //调用字符串输入并返回指针
    private static IntPtr CallString(delegate* unmanaged[Cdecl]<byte*, int, IntPtr> function, string? value)
    {
        if (function == null) return IntPtr.Zero;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length);
        }
    }

    //调用字符串和指针输入并返回指针
    private static IntPtr CallStringPointer(delegate* unmanaged[Cdecl]<byte*, int, IntPtr, IntPtr> function, string? value, IntPtr pointerValue)
    {
        if (function == null) return IntPtr.Zero;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length, pointerValue);
        }
    }

    //读取对象字符串
    private static string GetString(delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> function, IntPtr pointer)
    {
        if (function == null || pointer == IntPtr.Zero) return string.Empty;

        int requiredBytes = function(pointer, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = function(pointer, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return Encoding.UTF8.GetString(output[..length]);
        }
    }

    //调用槽位 bool 函数
    private static bool CallSlotBool(delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte> function, IntPtr material, string slotName)
    {
        if (!initialized || material == IntPtr.Zero || function == null) return false;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            return function(material, slotPointer, slotBytes.Length) != 0;
        }
    }

    //读取槽位字符串
    private static string GetSlotString(delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte*, int, int> function, IntPtr material, string slotName)
    {
        if (!initialized || material == IntPtr.Zero || function == null) return string.Empty;

        byte[] slotBytes = Encoding.UTF8.GetBytes(slotName ?? string.Empty);
        fixed (byte* slotPointer = slotBytes)
        {
            int requiredBytes = function(material, slotPointer, slotBytes.Length, null, 0);
            if (requiredBytes <= 0) return string.Empty;

            Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
            fixed (byte* outputPointer = output)
            {
                int actualBytes = function(material, slotPointer, slotBytes.Length, outputPointer, output.Length);
                int length = Math.Clamp(actualBytes, 0, output.Length);
                return Encoding.UTF8.GetString(output[..length]);
            }
        }
    }
}



public sealed unsafe partial class Shader
{
private static ShaderBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 Shader 函数表
    internal static void InitializeNativeApi(ShaderBindApi value)
    {
        api = value;
        initialized = api.Load != null;
    }

    //加载 Shader 资源
    internal static IntPtr LoadNative(string key) => initialized ? CallString(api.Load, key) : IntPtr.Zero;

    //创建运行时 Shader
    internal static IntPtr CreateFromSourceNative(string name, string vertexSource, string fragmentSource)
    {
        if (!initialized || api.CreateFromSource == null) return IntPtr.Zero;

        byte[] nameBytes = Encoding.UTF8.GetBytes(name ?? string.Empty);
        byte[] vertexBytes = Encoding.UTF8.GetBytes(vertexSource ?? string.Empty);
        byte[] fragmentBytes = Encoding.UTF8.GetBytes(fragmentSource ?? string.Empty);
        fixed (byte* namePointer = nameBytes)
        fixed (byte* vertexPointer = vertexBytes)
        fixed (byte* fragmentPointer = fragmentBytes)
        {
            return api.CreateFromSource(namePointer, nameBytes.Length, vertexPointer, vertexBytes.Length, fragmentPointer, fragmentBytes.Length);
        }
    }

    //判断 Shader 是否有效
    internal static bool IsNativeValid(IntPtr shader) => initialized && shader != IntPtr.Zero && api.IsValid(shader) != 0;

    //读取 Shader 名称
    internal static string GetName(IntPtr shader) => initialized ? GetString(api.GetName, shader) : string.Empty;

    //判断 Shader 是否需要刷新 GPU 数据
    internal static bool IsDirty(IntPtr shader) => initialized && shader != IntPtr.Zero && api.IsDirty != null && api.IsDirty(shader) != 0;

    //标记 Shader 需要刷新 GPU 数据
    internal static void MarkDirty(IntPtr shader)
    {
        if (initialized && shader != IntPtr.Zero && api.MarkDirty != null) api.MarkDirty(shader);
    }

    //读取顶点源码路径
    internal static string GetVertexPath(IntPtr shader) => initialized ? GetString(api.GetVertexPath, shader) : string.Empty;

    //读取片元源码路径
    internal static string GetFragmentPath(IntPtr shader) => initialized ? GetString(api.GetFragmentPath, shader) : string.Empty;

    //读取纹理槽数量
    internal static int GetTextureSlotCount(IntPtr shader) => initialized && shader != IntPtr.Zero ? api.GetTextureSlotCount(shader) : 0;

    //读取颜色槽数量
    internal static int GetColorSlotCount(IntPtr shader) => initialized && shader != IntPtr.Zero ? api.GetColorSlotCount(shader) : 0;

    //读取浮点槽数量
    internal static int GetFloatSlotCount(IntPtr shader) => initialized && shader != IntPtr.Zero ? api.GetFloatSlotCount(shader) : 0;

    //读取 Pass 数量
    internal static int GetPassCount(IntPtr shader) => initialized && shader != IntPtr.Zero ? api.GetPassCount(shader) : 0;

    //读取 Pass 名称
    internal static string GetPassName(IntPtr shader, int index) => initialized ? GetString(api.GetPassName, shader, index) : string.Empty;

    //读取 Pass 深度测试状态
    internal static int GetPassDepthTest(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetPassDepthTest(shader, index) : 0;

    //读取 Pass 深度写入状态
    internal static int GetPassDepthWrite(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetPassDepthWrite(shader, index) : 0;

    //读取 Pass 混合状态
    internal static int GetPassBlend(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetPassBlend(shader, index) : 0;

    //读取 Pass 剔除状态
    internal static int GetPassCull(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetPassCull(shader, index) : 0;

    //读取纹理槽名称
    internal static string GetTextureSlotName(IntPtr shader, int index) => initialized ? GetString(api.GetTextureSlotName, shader, index) : string.Empty;

    //读取纹理槽显示名
    internal static string GetTextureSlotDisplayName(IntPtr shader, int index) => initialized ? GetString(api.GetTextureSlotDisplayName, shader, index) : string.Empty;

    //读取纹理槽维度
    internal static int GetTextureSlotDimension(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetTextureSlotDimension(shader, index) : 0;

    //读取颜色槽名称
    internal static string GetColorSlotName(IntPtr shader, int index) => initialized ? GetString(api.GetColorSlotName, shader, index) : string.Empty;

    //读取颜色槽显示名
    internal static string GetColorSlotDisplayName(IntPtr shader, int index) => initialized ? GetString(api.GetColorSlotDisplayName, shader, index) : string.Empty;

    //读取颜色槽默认值
    internal static color4 GetColorSlotDefault(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetColorSlotDefault(shader, index) : default;

    //读取浮点槽名称
    internal static string GetFloatSlotName(IntPtr shader, int index) => initialized ? GetString(api.GetFloatSlotName, shader, index) : string.Empty;

    //读取浮点槽显示名
    internal static string GetFloatSlotDisplayName(IntPtr shader, int index) => initialized ? GetString(api.GetFloatSlotDisplayName, shader, index) : string.Empty;

    //读取浮点槽默认值
    internal static float GetFloatSlotDefault(IntPtr shader, int index) => initialized && shader != IntPtr.Zero ? api.GetFloatSlotDefault(shader, index) : 0.0f;

    //替换 Shader 源码
    internal static bool ReplaceSource(IntPtr shader, string vertexSource, string fragmentSource)
    {
        if (!initialized || shader == IntPtr.Zero || api.ReplaceSource == null) return false;

        byte[] vertexBytes = Encoding.UTF8.GetBytes(vertexSource ?? string.Empty);
        byte[] fragmentBytes = Encoding.UTF8.GetBytes(fragmentSource ?? string.Empty);
        fixed (byte* vertexPointer = vertexBytes)
        fixed (byte* fragmentPointer = fragmentBytes)
        {
            return api.ReplaceSource(shader, vertexPointer, vertexBytes.Length, fragmentPointer, fragmentBytes.Length) != 0;
        }
    }

    //调用字符串输入并返回指针
    private static IntPtr CallString(delegate* unmanaged[Cdecl]<byte*, int, IntPtr> function, string? value)
    {
        if (function == null) return IntPtr.Zero;

        byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        fixed (byte* pointer = bytes)
        {
            return function(pointer, bytes.Length);
        }
    }

    //读取对象字符串
    private static string GetString(delegate* unmanaged[Cdecl]<IntPtr, byte*, int, int> function, IntPtr pointer)
    {
        if (function == null || pointer == IntPtr.Zero) return string.Empty;

        int requiredBytes = function(pointer, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = function(pointer, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return Encoding.UTF8.GetString(output[..length]);
        }
    }

    //读取对象索引字符串
    private static string GetString(delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> function, IntPtr pointer, int index)
    {
        if (function == null || pointer == IntPtr.Zero) return string.Empty;

        int requiredBytes = function(pointer, index, null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* outputPointer = output)
        {
            int actualBytes = function(pointer, index, outputPointer, output.Length);
            int length = Math.Clamp(actualBytes, 0, output.Length);
            return Encoding.UTF8.GetString(output[..length]);
        }
    }
}
