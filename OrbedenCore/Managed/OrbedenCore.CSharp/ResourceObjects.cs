namespace OrbedenCore.CSharp;

/// <summary>资源对象托管代理基类。</summary>
public abstract class ResourceObject
{
    /// <summary>资源 Key。</summary>
    public string Key { get; }

    /// <summary>资源 Key。</summary>
    public string key => Key;

    /// <summary>创建资源对象托管代理。</summary>
    protected ResourceObject(string key)
    {
        Key = key ?? string.Empty;
    }

    /// <summary>判断资源是否已加载且类型正确。</summary>
    public abstract bool IsValid { get; }

    /// <summary>返回资源 Key。</summary>
    public override string ToString()
    {
        return Key;
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

/// <summary>CPU Mesh 资源托管代理。</summary>
public sealed class Mesh : ResourceObject
{
    /// <summary>创建 Mesh 资源托管代理。</summary>
    private Mesh(string key) : base(key) {}

    /// <summary>通过资源 Key 创建代理，不主动加载。</summary>
    internal static Mesh? FromKey(string key)
    {
        return string.IsNullOrEmpty(key) ? null : new Mesh(key);
    }

    /// <summary>加载 Mesh 资源。</summary>
    public static Mesh? Load(string key)
    {
        return MeshBind.Load(key) ? new Mesh(key) : null;
    }

    /// <summary>判断资源是否已加载且类型正确。</summary>
    public override bool IsValid => MeshBind.IsValid(Key);

    /// <summary>Mesh 名称。</summary>
    public string name => MeshBind.GetName(Key);

    /// <summary>顶点数量。</summary>
    public int vertexCount => MeshBind.GetVertexCount(Key);

    /// <summary>索引数量。</summary>
    public int indexCount => MeshBind.GetIndexCount(Key);

    /// <summary>子网格数量。</summary>
    public int subMeshCount => MeshBind.GetSubMeshCount(Key);

    /// <summary>读取子网格信息。</summary>
    public SubMeshInfo GetSubMesh(int index)
    {
        string materialKey = MeshBind.GetSubMeshMaterial(Key, index);
        return new SubMeshInfo(
            MeshBind.GetSubMeshName(Key, index),
            MeshBind.GetSubMeshIndexStart(Key, index),
            MeshBind.GetSubMeshIndexCount(Key, index),
            Material.FromKey(materialKey));
    }
}

/// <summary>CPU Material 资源托管代理。</summary>
public sealed class Material : ResourceObject
{
    /// <summary>创建 Material 资源托管代理。</summary>
    private Material(string key) : base(key) {}

    /// <summary>通过资源 Key 创建代理，不主动加载。</summary>
    internal static Material? FromKey(string key)
    {
        return string.IsNullOrEmpty(key) ? null : new Material(key);
    }

    /// <summary>加载 Material 资源。</summary>
    public static Material? Load(string key)
    {
        return MaterialBind.Load(key) ? new Material(key) : null;
    }

    /// <summary>判断资源是否已加载且类型正确。</summary>
    public override bool IsValid => MaterialBind.IsValid(Key);

    /// <summary>Material 名称。</summary>
    public string name => MaterialBind.GetName(Key);

    /// <summary>材质版本。</summary>
    public ulong revision => MaterialBind.GetRevision(Key);

    /// <summary>材质使用的 Shader。</summary>
    public Shader? shader
    {
        get => Shader.FromKey(MaterialBind.GetShader(Key));
        set => MaterialBind.SetShader(Key, value?.Key ?? string.Empty);
    }

    /// <summary>判断纹理槽是否存在。</summary>
    public bool HasTexture(string slotName)
    {
        return MaterialBind.HasTexture(Key, slotName);
    }

    /// <summary>读取纹理资源 Key。</summary>
    public string GetTextureKey(string slotName)
    {
        return MaterialBind.GetTexture(Key, slotName);
    }

    /// <summary>写入纹理资源 Key。</summary>
    public bool SetTextureKey(string slotName, string textureKey)
    {
        return MaterialBind.SetTexture(Key, slotName, textureKey);
    }

    /// <summary>清除纹理槽。</summary>
    public bool ClearTexture(string slotName)
    {
        return MaterialBind.ClearTexture(Key, slotName);
    }

    /// <summary>判断颜色槽是否存在。</summary>
    public bool HasColor(string slotName)
    {
        return MaterialBind.HasColor(Key, slotName);
    }

    /// <summary>读取颜色槽。</summary>
    public color4 GetColor(string slotName)
    {
        return GetColor(slotName, new color4(0.0f, 0.0f, 0.0f, 1.0f));
    }

    /// <summary>读取颜色槽。</summary>
    public color4 GetColor(string slotName, color4 defaultValue)
    {
        return MaterialBind.GetColor(Key, slotName, defaultValue);
    }

    /// <summary>写入颜色槽。</summary>
    public bool SetColor(string slotName, color4 value)
    {
        return MaterialBind.SetColor(Key, slotName, value);
    }

    /// <summary>清除颜色槽。</summary>
    public bool ClearColor(string slotName)
    {
        return MaterialBind.ClearColor(Key, slotName);
    }

    /// <summary>判断浮点槽是否存在。</summary>
    public bool HasFloat(string slotName)
    {
        return MaterialBind.HasFloat(Key, slotName);
    }

    /// <summary>读取浮点槽。</summary>
    public float GetFloat(string slotName)
    {
        return GetFloat(slotName, 0.0f);
    }

    /// <summary>读取浮点槽。</summary>
    public float GetFloat(string slotName, float defaultValue)
    {
        return MaterialBind.GetFloat(Key, slotName, defaultValue);
    }

    /// <summary>写入浮点槽。</summary>
    public bool SetFloat(string slotName, float value)
    {
        return MaterialBind.SetFloat(Key, slotName, value);
    }

    /// <summary>清除浮点槽。</summary>
    public bool ClearFloat(string slotName)
    {
        return MaterialBind.ClearFloat(Key, slotName);
    }
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

/// <summary>CPU Shader 资源托管代理。</summary>
public sealed class Shader : ResourceObject
{
    /// <summary>创建 Shader 资源托管代理。</summary>
    private Shader(string key) : base(key) {}

    /// <summary>通过资源 Key 创建代理，不主动加载。</summary>
    internal static Shader? FromKey(string key)
    {
        return string.IsNullOrEmpty(key) ? null : new Shader(key);
    }

    /// <summary>加载 Shader 资源。</summary>
    public static Shader? Load(string key)
    {
        return ShaderBind.Load(key) ? new Shader(key) : null;
    }

    /// <summary>判断资源是否已加载且类型正确。</summary>
    public override bool IsValid => ShaderBind.IsValid(Key);

    /// <summary>Shader 名称。</summary>
    public string name => ShaderBind.GetName(Key);

    /// <summary>顶点源码路径。</summary>
    public string vertexPath => ShaderBind.GetVertexPath(Key);

    /// <summary>片元源码路径。</summary>
    public string fragmentPath => ShaderBind.GetFragmentPath(Key);

    /// <summary>纹理槽数量。</summary>
    public int textureSlotCount => ShaderBind.GetTextureSlotCount(Key);

    /// <summary>颜色槽数量。</summary>
    public int colorSlotCount => ShaderBind.GetColorSlotCount(Key);

    /// <summary>浮点槽数量。</summary>
    public int floatSlotCount => ShaderBind.GetFloatSlotCount(Key);

    /// <summary>读取纹理槽信息。</summary>
    public ShaderTextureSlotInfo GetTextureSlot(int index)
    {
        return new ShaderTextureSlotInfo(
            ShaderBind.GetTextureSlotName(Key, index),
            ShaderBind.GetTextureSlotDisplayName(Key, index),
            (ShaderTextureDimension)ShaderBind.GetTextureSlotDimension(Key, index));
    }

    /// <summary>读取颜色槽信息。</summary>
    public ShaderColorSlotInfo GetColorSlot(int index)
    {
        return new ShaderColorSlotInfo(
            ShaderBind.GetColorSlotName(Key, index),
            ShaderBind.GetColorSlotDisplayName(Key, index),
            ShaderBind.GetColorSlotDefault(Key, index));
    }

    /// <summary>读取浮点槽信息。</summary>
    public ShaderFloatSlotInfo GetFloatSlot(int index)
    {
        return new ShaderFloatSlotInfo(
            ShaderBind.GetFloatSlotName(Key, index),
            ShaderBind.GetFloatSlotDisplayName(Key, index),
            ShaderBind.GetFloatSlotDefault(Key, index));
    }
}
