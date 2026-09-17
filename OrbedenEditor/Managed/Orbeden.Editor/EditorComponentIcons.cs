namespace OrbedenEditor;

/// <summary>把组件与引用类型映射到 Editor 资源目录中的图标名。</summary>
internal static class EditorComponentIcons
{
    private const string Fallback = "Other";

    /// <summary>按组件类型与脚本域取图标名，引擎未收录的类型回落通用图标。</summary>
    public static string Resolve(string typeName, bool isManaged)
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

    /// <summary>按引用字段的声明类型取图标名，资源引用与未知类型回落通用图标。</summary>
    public static string ResolveReference(string referenceType)
    {
        int separator = referenceType.LastIndexOf('.');
        string shortName = separator >= 0 ? referenceType[(separator + 1)..] : referenceType;
        switch (shortName)
        {
            case "Transform": return "Transform";
            case "StaticMeshRenderer": return "Renderer";
            case "RigidBody": return "RigidBody";
        }
        return shortName.Contains("Collider", StringComparison.Ordinal) ? "Collider" : Fallback;
    }
}
