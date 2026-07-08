using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>原生对象托管包装基类。</summary>
public abstract class Object
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

    /// <summary>判断原生对象是否仍然存活。</summary>
    public bool IsAlive => instanceId != 0 && ObjectBind.IsAlive(instanceId);

    /// <summary>判断对象是否有效。</summary>
    public virtual bool IsValid => IsAlive;

    internal IntPtr NativePtr => IsAlive ? nativePtr : IntPtr.Zero;

    /// <summary>销毁原生对象。</summary>
    public static bool Destroy(Object? target)
    {
        if (target == null || !target.IsAlive) return false;

        bool destroyed = ObjectBind.Destroy(target.NativePtr);
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

        int id = ObjectBind.GetInstanceId(pointer);
        if (id == 0) return;

        nativePtr = pointer;
        instanceId = id;

        lock (cacheLock)
        {
            if (cache.TryGetValue(id, out WrapperEntry? oldEntry) && oldEntry.handle != IntPtr.Zero)
            {
                ObjectBind.SetManagedWrapper(oldEntry.nativePtr, IntPtr.Zero);
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

            ObjectBind.SetManagedWrapper(pointer, handlePtr);
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
                if (ObjectBind.IsAlive(instanceId)) ObjectBind.SetManagedWrapper(entry.nativePtr, IntPtr.Zero);
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

        IntPtr handle = ObjectBind.GetManagedWrapper(pointer);
        if (handle != IntPtr.Zero && GCHandle.FromIntPtr(handle).Target is T cached && cached.IsAlive)
        {
            return cached;
        }

        int id = ObjectBind.GetInstanceId(pointer);
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

                if (ObjectBind.IsAlive(id)) ObjectBind.SetManagedWrapper(entry.nativePtr, IntPtr.Zero);
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
        return ObjectBind.UnloadUnusedObjects(Object.CollectManagedRootIds());
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
public sealed class Mesh : Object
{
    /// <summary>创建运行时 Mesh。</summary>
    public Mesh() : this("Mesh") {}

    /// <summary>创建运行时 Mesh。</summary>
    public Mesh(string name)
    {
        ConnectNative(MeshBind.Create(name));
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
        return FromNative(MeshBind.Load(key));
    }

    /// <summary>判断资源是否已加载且类型正确。</summary>
    public override bool IsValid => MeshBind.IsValid(NativePtr);

    /// <summary>Mesh 名称。</summary>
    public string name
    {
        get => MeshBind.GetName(NativePtr);
        set => MeshBind.SetName(NativePtr, value);
    }

    /// <summary>Mesh 版本。</summary>
    public ulong revision => MeshBind.GetRevision(NativePtr);

    /// <summary>顶点数量。</summary>
    public int vertexCount => MeshBind.GetVertexCount(NativePtr);

    /// <summary>索引数量。</summary>
    public int indexCount => MeshBind.GetIndexCount(NativePtr);

    /// <summary>子网格数量。</summary>
    public int subMeshCount => MeshBind.GetSubMeshCount(NativePtr);

    /// <summary>顶点位置数组副本。</summary>
    public vector3[] vertexPositions
    {
        get => MeshBind.GetVertexPositions(NativePtr);
        set => SetVertexPositions(value);
    }

    /// <summary>顶点法线数组副本。</summary>
    public vector3[] vertexNormals
    {
        get => MeshBind.GetVertexNormals(NativePtr);
        set => SetVertexNormals(value);
    }

    /// <summary>顶点 UV 数组副本。</summary>
    public vector2[] vertexTexcoords
    {
        get => MeshBind.GetVertexTexcoords(NativePtr);
        set => SetVertexTexcoords(value);
    }

    /// <summary>顶点切线数组副本。</summary>
    public vector3[] vertexTangents
    {
        get => MeshBind.GetVertexTangents(NativePtr);
        set => SetVertexTangents(value);
    }

    /// <summary>索引数组副本。</summary>
    public uint[] indexData
    {
        get => MeshBind.GetIndexData(NativePtr);
        set => SetIndexData(value);
    }

    /// <summary>读取子网格信息。</summary>
    public SubMeshInfo GetSubMesh(int index)
    {
        return new SubMeshInfo(
            MeshBind.GetSubMeshName(NativePtr, index),
            MeshBind.GetSubMeshIndexStart(NativePtr, index),
            MeshBind.GetSubMeshIndexCount(NativePtr, index),
            Material.FromNative(MeshBind.GetSubMeshMaterial(NativePtr, index)));
    }

    /// <summary>写入顶点位置数组。</summary>
    public bool SetVertexPositions(vector3[]? values) => MeshBind.SetVertexPositions(NativePtr, values);

    /// <summary>写入顶点法线数组。</summary>
    public bool SetVertexNormals(vector3[]? values) => MeshBind.SetVertexNormals(NativePtr, values);

    /// <summary>写入顶点 UV 数组。</summary>
    public bool SetVertexTexcoords(vector2[]? values) => MeshBind.SetVertexTexcoords(NativePtr, values);

    /// <summary>写入顶点切线数组。</summary>
    public bool SetVertexTangents(vector3[]? values) => MeshBind.SetVertexTangents(NativePtr, values);

    /// <summary>写入索引数组。</summary>
    public bool SetIndexData(uint[]? values) => MeshBind.SetIndexData(NativePtr, values);

    /// <summary>清空几何数据。</summary>
    public bool ClearGeometry() => MeshBind.ClearGeometry(NativePtr);

    /// <summary>根据三角形索引重新计算法线。</summary>
    public bool RefreshNormals() => MeshBind.RefreshNormals(NativePtr);

    /// <summary>调整子网格数量。</summary>
    public bool ResizeSubMeshes(int count) => MeshBind.ResizeSubMeshes(NativePtr, count);

    /// <summary>配置子网格。</summary>
    public bool ConfigureSubMesh(int index, string name, uint indexStart, uint indexCount, Material? material)
    {
        return MeshBind.ConfigureSubMesh(NativePtr, index, name, indexStart, indexCount, material?.NativePtr ?? IntPtr.Zero);
    }
}

/// <summary>CPU Material 资源托管包装。</summary>
public sealed class Material : Object
{
    /// <summary>创建运行时 Material。</summary>
    public Material(Shader? shader) : this(shader, "Material") {}

    /// <summary>创建运行时 Material。</summary>
    public Material(Shader? shader, string name)
    {
        ConnectNative(MaterialBind.Create(name, shader?.NativePtr ?? IntPtr.Zero));
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
        return FromNative(MaterialBind.Load(key));
    }

    /// <summary>判断资源是否已加载且类型正确。</summary>
    public override bool IsValid => MaterialBind.IsValid(NativePtr);

    /// <summary>Material 名称。</summary>
    public string name => MaterialBind.GetName(NativePtr);

    /// <summary>材质版本。</summary>
    public ulong revision => MaterialBind.GetRevision(NativePtr);

    /// <summary>材质使用的 Shader。</summary>
    public Shader? shader
    {
        get => Shader.FromNative(MaterialBind.GetShader(NativePtr));
        set => MaterialBind.SetShader(NativePtr, value?.NativePtr ?? IntPtr.Zero);
    }

    /// <summary>判断纹理槽是否存在。</summary>
    public bool HasTexture(string slotName) => MaterialBind.HasTexture(NativePtr, slotName);

    /// <summary>读取纹理资源 Key。</summary>
    public string GetTextureKey(string slotName) => MaterialBind.GetTexture(NativePtr, slotName);

    /// <summary>读取纹理资源 Key。</summary>
    public string GetTextureResource(string slotName) => GetTextureKey(slotName);

    /// <summary>写入纹理资源 Key。</summary>
    public bool SetTextureKey(string slotName, string textureKey) => MaterialBind.SetTexture(NativePtr, slotName, textureKey);

    /// <summary>写入纹理资源 Key。</summary>
    public bool SetTextureResource(string slotName, string textureKey) => SetTextureKey(slotName, textureKey);

    /// <summary>清除纹理槽。</summary>
    public bool ClearTexture(string slotName) => MaterialBind.ClearTexture(NativePtr, slotName);

    /// <summary>判断颜色槽是否存在。</summary>
    public bool HasColor(string slotName) => MaterialBind.HasColor(NativePtr, slotName);

    /// <summary>读取颜色槽。</summary>
    public color4 GetColor(string slotName) => GetColor(slotName, new color4(0.0f, 0.0f, 0.0f, 1.0f));

    /// <summary>读取颜色槽。</summary>
    public color4 GetColor(string slotName, color4 defaultValue) => MaterialBind.GetColor(NativePtr, slotName, defaultValue);

    /// <summary>读取颜色槽。</summary>
    public color4 GetColorValue(string slotName) => GetColor(slotName);

    /// <summary>读取颜色槽。</summary>
    public color4 GetColorValue(string slotName, color4 defaultValue) => GetColor(slotName, defaultValue);

    /// <summary>写入颜色槽。</summary>
    public bool SetColor(string slotName, color4 value) => MaterialBind.SetColor(NativePtr, slotName, value);

    /// <summary>写入颜色槽。</summary>
    public bool SetColorValue(string slotName, color4 value) => SetColor(slotName, value);

    /// <summary>清除颜色槽。</summary>
    public bool ClearColor(string slotName) => MaterialBind.ClearColor(NativePtr, slotName);

    /// <summary>判断浮点槽是否存在。</summary>
    public bool HasFloat(string slotName) => MaterialBind.HasFloat(NativePtr, slotName);

    /// <summary>读取浮点槽。</summary>
    public float GetFloat(string slotName) => GetFloat(slotName, 0.0f);

    /// <summary>读取浮点槽。</summary>
    public float GetFloat(string slotName, float defaultValue) => MaterialBind.GetFloat(NativePtr, slotName, defaultValue);

    /// <summary>读取浮点槽。</summary>
    public float GetFloatValue(string slotName) => GetFloat(slotName);

    /// <summary>读取浮点槽。</summary>
    public float GetFloatValue(string slotName, float defaultValue) => GetFloat(slotName, defaultValue);

    /// <summary>写入浮点槽。</summary>
    public bool SetFloat(string slotName, float value) => MaterialBind.SetFloat(NativePtr, slotName, value);

    /// <summary>写入浮点槽。</summary>
    public bool SetFloatValue(string slotName, float value) => SetFloat(slotName, value);

    /// <summary>清除浮点槽。</summary>
    public bool ClearFloat(string slotName) => MaterialBind.ClearFloat(NativePtr, slotName);
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

/// <summary>CPU Shader 资源托管包装。</summary>
public sealed class Shader : Object
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
        return FromNative(ShaderBind.Load(key));
    }

    /// <summary>从 GLSL 源码创建运行时 Shader。</summary>
    public static Shader? CreateFromSource(string name, string vertexSource, string fragmentSource)
    {
        return FromNative(ShaderBind.CreateFromSource(name, vertexSource, fragmentSource));
    }

    /// <summary>判断资源是否已加载且类型正确。</summary>
    public override bool IsValid => ShaderBind.IsValid(NativePtr);

    /// <summary>Shader 名称。</summary>
    public string name => ShaderBind.GetName(NativePtr);

    /// <summary>Shader 版本。</summary>
    public ulong revision => ShaderBind.GetRevision(NativePtr);

    /// <summary>顶点源码路径。</summary>
    public string vertexPath => ShaderBind.GetVertexPath(NativePtr);

    /// <summary>片元源码路径。</summary>
    public string fragmentPath => ShaderBind.GetFragmentPath(NativePtr);

    /// <summary>纹理槽数量。</summary>
    public int textureSlotCount => ShaderBind.GetTextureSlotCount(NativePtr);

    /// <summary>颜色槽数量。</summary>
    public int colorSlotCount => ShaderBind.GetColorSlotCount(NativePtr);

    /// <summary>浮点槽数量。</summary>
    public int floatSlotCount => ShaderBind.GetFloatSlotCount(NativePtr);

    /// <summary>替换 GLSL 源码并刷新材质槽反射结果。</summary>
    public bool ReplaceSource(string vertexSource, string fragmentSource)
    {
        return ShaderBind.ReplaceSource(NativePtr, vertexSource, fragmentSource);
    }

    /// <summary>读取纹理槽信息。</summary>
    public ShaderTextureSlotInfo GetTextureSlot(int index)
    {
        return new ShaderTextureSlotInfo(
            ShaderBind.GetTextureSlotName(NativePtr, index),
            ShaderBind.GetTextureSlotDisplayName(NativePtr, index),
            (ShaderTextureDimension)ShaderBind.GetTextureSlotDimension(NativePtr, index));
    }

    /// <summary>读取颜色槽信息。</summary>
    public ShaderColorSlotInfo GetColorSlot(int index)
    {
        return new ShaderColorSlotInfo(
            ShaderBind.GetColorSlotName(NativePtr, index),
            ShaderBind.GetColorSlotDisplayName(NativePtr, index),
            ShaderBind.GetColorSlotDefault(NativePtr, index));
    }

    /// <summary>读取浮点槽信息。</summary>
    public ShaderFloatSlotInfo GetFloatSlot(int index)
    {
        return new ShaderFloatSlotInfo(
            ShaderBind.GetFloatSlotName(NativePtr, index),
            ShaderBind.GetFloatSlotDisplayName(NativePtr, index),
            ShaderBind.GetFloatSlotDefault(NativePtr, index));
    }
}
