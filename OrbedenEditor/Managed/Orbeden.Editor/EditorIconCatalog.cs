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
        //这里的名单只管选图标。判断某组件是不是用户脚本要看它是否继承 Script，
        //不要拿这份名单当判据：新增内建组件时它会漏，见 InspectorPanel.GetComponentTitle
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

    /// <summary>目录使用文件夹图标，原始文件交给独立的源文件图标映射。</summary>
    public static string ForResource(string path, bool isDirectory)
        => isDirectory ? "Folder" : EditorSourceIconCatalog.ForFile(path);

    //取类型名去掉命名空间后的短名
    private static string ShortName(string typeName)
    {
        int separator = typeName.LastIndexOf('.');
        return separator >= 0 ? typeName[(separator + 1)..] : typeName;
    }
}
