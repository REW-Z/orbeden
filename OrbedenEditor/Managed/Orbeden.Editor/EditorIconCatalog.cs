namespace OrbedenEditor;

/// <summary>把组件类型、引用类型与资源路径映射到 Editor 资源目录中的图标名。</summary>
internal static class EditorIconCatalog
{
    private const string Fallback = "Other";

    /// <summary>按组件类型与脚本域取图标名，引擎未收录的类型回落通用图标。</summary>
    public static string ForComponent(string typeName, bool isManaged)
    {
        if (isManaged) return "CSharpScript";
        switch (typeName)
        {
            case "Transform": return "Transform";
            case "StaticMeshRenderer": return "Renderer";
            case "RigidBody": return "RigidBody";
            case "Camera":
            case "CharacterController":
            case "DirectionalLight":
            case "HeightField": return Fallback;
        }
        //引擎自带组件只有上面这些，其余非托管组件都是 C++ 脚本
        return typeName.Contains("Collider", StringComparison.Ordinal) ? "Collider" : "CppScript";
    }

    /// <summary>按引用字段的声明类型取图标名，只认识确切的组件与资源类型。</summary>
    public static string ForReference(string referenceType)
    {
        string shortName = ShortName(referenceType);
        if (shortName.Contains("Collider", StringComparison.Ordinal)) return "Collider";
        return shortName switch
        {
            "Transform" => "Transform",
            "StaticMeshRenderer" => "Renderer",
            "RigidBody" => "RigidBody",
            "Mesh" => "MeshFile",
            "Material" => "Material",
            "Shader" => "ShaderFile",
            "Texture2D" or "Texture" => "TextureFile",
            _ => Fallback,
        };
    }

    /// <summary>按资源路径取图标名，目录用文件夹图标，文件按扩展名。</summary>
    public static string ForResource(string path, bool isDirectory)
    {
        if (isDirectory) return "Folder";
        return Path.GetExtension(path).ToLowerInvariant() switch
        {
            ".obj" or ".gltf" or ".glb" => "MeshFile",
            ".mtl" => "Material",
            ".orbshader" or ".vert" or ".frag" or ".glsl" => "ShaderFile",
            ".png" or ".jpg" or ".jpeg" or ".tga" or ".bmp" => "TextureFile",
            ".cs" => "CSharpScript",
            ".cpp" or ".cc" or ".cxx" or ".c" or ".h" or ".hpp" or ".inl" => "CppScript",
            ".txt" or ".md" or ".json" or ".xml" or ".csv" or ".log" or ".ini" or ".yaml" or ".yml" => "TextFile",
            _ => Fallback,
        };
    }

    //取类型名去掉命名空间后的短名
    private static string ShortName(string typeName)
    {
        int separator = typeName.LastIndexOf('.');
        return separator >= 0 ? typeName[(separator + 1)..] : typeName;
    }
}
