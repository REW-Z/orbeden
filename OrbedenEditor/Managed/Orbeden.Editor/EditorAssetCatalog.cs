using System.Diagnostics;
using Orbeden;

namespace OrbedenEditor;

/// <summary>索引 ProjectPanel 和 ObjectField 使用的项目资源。</summary>
internal sealed class EditorAssetCatalog : IObjectFieldAssetProvider
{
    private string indexedContentRoot = string.Empty;

    public static EditorAssetCatalog Instance { get; } = new();

    /// <summary>内容根：资源、场景与脚本的根，内部结构完全自由。</summary>
    public string ContentRoot => Path.GetFullPath(PathDefines.ContentRoot);

    /// <summary>判断某个路径是否落在内容根之外。</summary>
    public bool IsGeneratedPath(string fullPath)
    {
        return !IsInsideContentRoot(fullPath) || fullPath.EndsWith(".resinfo", StringComparison.OrdinalIgnoreCase)
            || fullPath.EndsWith(".resinfo.tmp", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>判断某个路径是否落在内容根内。</summary>
    public bool IsInsideContentRoot(string fullPath)
    {
        string contentRoot = ContentRoot;
        if (string.IsNullOrWhiteSpace(contentRoot)) return false;

        string relative = Path.GetRelativePath(contentRoot, Path.GetFullPath(fullPath));
        return !relative.StartsWith("..", StringComparison.Ordinal) && !Path.IsPathRooted(relative);
    }

    /// <summary>读取指定对象类型的可选择资源。</summary>
    public IReadOnlyList<ObjectFieldOption> GetAssets(Type objectType)
    {
        EnsureCurrentProject();
        EditorAssetCache.Update();
        return EditorAssetCache.GetOptions(objectType).DistinctBy(option => option.ResourceKey).ToList();
    }

    /// <summary>按资源 Key 加载一个强类型资源包装。</summary>
    public Orbeden.Object? Load(Type objectType, string resourceKey)
    {
        if (EditorAssetCache.TryLoad(resourceKey, objectType, out Orbeden.Object? cached)) return cached;
        Type actualType = objectType;
        if (!typeof(Orbeden.Object).IsAssignableFrom(actualType) || actualType.IsAbstract) return null;
        return typeof(Resources).GetMethod(nameof(Resources.Load))!.MakeGenericMethod(actualType)
            .Invoke(null, [resourceKey]) as Orbeden.Object;
    }

    /// <summary>重新扫描当前项目资源。</summary>
    public void Refresh()
    {
        EditorAssetInspection.Invalidate();
        EditorAssetCache.BeginScan();
        indexedContentRoot = PathDefines.ContentRoot;
        if (string.IsNullOrWhiteSpace(indexedContentRoot)) return;

        string root = ContentRoot;
        if (!Directory.Exists(root)) return;

        foreach (string file in EnumerateSourceFiles(root))
        {
            if (EditorAssetInspection.CanInspect(file)) EditorAssetCache.Queue(file);
        }
        EditorAssetCache.EndScan();

    }

    //把资源磁盘路径转换为内容根相对 Key。内容根之外的路径返回空：
    //Key 最终由原生侧按内容根解析，带 ".." 的 Key 能读到内容根外的文件，必须在这里挡住。
    public string ToResourceKey(string fullPath)
    {
        string relative = Path.GetRelativePath(ContentRoot, Path.GetFullPath(fullPath));
        if (relative.StartsWith("..", StringComparison.Ordinal) || Path.IsPathRooted(relative)) return string.Empty;
        return NormalizeKey(relative);
    }

    //递归枚举内容根下的资源源文件。内容根之外按定义不是用户内容，不需要排除表；
    //只跳过以点开头的目录（版本库、编辑器缓存等）。
    private static IEnumerable<string> EnumerateSourceFiles(string contentRoot)
    {
        Stack<string> pending = new();
        pending.Push(contentRoot);
        while (pending.Count > 0)
        {
            string directory = pending.Pop();
            foreach (string child in Directory.EnumerateDirectories(directory))
            {
                if (Path.GetFileName(child).StartsWith('.') || (File.GetAttributes(child) & FileAttributes.ReparsePoint) != 0) continue;
                pending.Push(child);
            }

            foreach (string file in Directory.EnumerateFiles(directory))
            {
                yield return file;
            }
        }
    }

    /// <summary>识别不展开内部对象的 Shader 与脚本代码文件。</summary>
    public static bool IsCodeFile(string path) => Path.GetExtension(path).ToLowerInvariant() is
        ".orbshader" or ".glsl" or ".vert" or ".frag" or ".cs"
        or ".cpp" or ".cc" or ".cxx" or ".c" or ".h" or ".hpp" or ".inl";

    /// <summary>识别使用文本文档图标的配置与纯文本文件。</summary>
    public static bool IsTextFile(string path) => Path.GetExtension(path).ToLowerInvariant() is
        ".txt" or ".md" or ".json" or ".xml" or ".csv" or ".tsv" or ".log"
        or ".ini" or ".cfg" or ".conf" or ".toml" or ".yaml" or ".yml" or ".layers";

    /// <summary>仅为可承载导入对象的原始文件提供展开入口。</summary>
    public static bool CanExpandSource(string path) => !IsCodeFile(path) && !IsTextFile(path)
        && !Path.GetExtension(path).Equals(".world", StringComparison.OrdinalIgnoreCase);

    /// <summary>返回资源文件在 ProjectPanel 中显示的类型。</summary>
    public string GetSourceType(string path)
    {
        if (Directory.Exists(path)) return "Folder";

        return Path.GetExtension(path).ToLowerInvariant() switch
        {
            ".obj" => "Mesh Source",
            ".gltf" or ".glb" => "glTF Source",
            ".mtl" => "Material Source",
            ".orbshader" => "Shader",
            ".world" => "World",
            ".vert" or ".frag" or ".glsl" => "Shader Source",
            ".png" or ".jpg" or ".jpeg" or ".tga" or ".bmp" => "Texture2D",
            _ => "File",
        };
    }

    /// <summary>用系统默认程序打开资源文件。</summary>
    public static void OpenFile(string path)
    {
        if (!File.Exists(path)) return;
        Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
    }

    /// <summary>在系统文件管理器中定位资源。</summary>
    public static void Reveal(string path)
    {
        string fullPath = Path.GetFullPath(path);
        string arguments = File.Exists(fullPath) ? $"/select,\"{fullPath}\"" : $"\"{fullPath}\"";
        Process.Start(new ProcessStartInfo("explorer.exe", arguments) { UseShellExecute = true });
    }

    //检测项目变化并刷新索引。
    private void EnsureCurrentProject()
    {
        if (!string.Equals(indexedContentRoot, PathDefines.ContentRoot, StringComparison.OrdinalIgnoreCase))
        {
            Refresh();
        }
    }

    private static string NormalizeKey(string path)
    {
        string value = path.Replace('\\', '/');
        while (value.StartsWith("./", StringComparison.Ordinal)) value = value[2..];
        return value;
    }
}
