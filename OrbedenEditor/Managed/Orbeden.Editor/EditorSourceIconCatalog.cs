namespace OrbedenEditor;

/// <summary>原始资源文件的独立图标映射，不使用导入对象的类型图标。</summary>
internal static class EditorSourceIconCatalog
{
    /// <summary>World、代码和文本文件使用专用图标，其余原始资源使用纸箱图标。</summary>
    public static string ForFile(string path) => Path.GetExtension(path).ToLowerInvariant() switch
    {
        ".orbshader" or ".glsl" or ".vert" or ".frag" => "SourceFiles/ShaderFile",
        ".cs" => "SourceFiles/CSharpScript",
        ".world" => "SourceFiles/World",
        _ => EditorAssetCatalog.IsCodeFile(path) ? "SourceFiles/CppScript"
            : EditorAssetCatalog.IsTextFile(path) ? "SourceFiles/TextFile" : "SourceFiles/Fallback",
    };
}
