namespace OrbedenEditor;

/// <summary>把脚本类型解析到它在内容根里的源文件：托管脚本找 .cs，原生脚本找 .h/.cpp。</summary>
internal static class EditorScriptFiles
{
    //最近一次命中的解析结果。菜单开着时每帧都会问同一个类型，缓存命中就不必反复扫盘
    private static string cachedTypeName = string.Empty;
    private static bool cachedManaged;
    private static string cachedContentRoot = string.Empty;
    private static string cachedPath = string.Empty;

    /// <summary>按类型名查找脚本源文件，找不到返回空路径。isManaged 决定找 .cs 还是 C++ 源文件。</summary>
    internal static string FindSourcePath(string typeName, bool isManaged)
    {
        if (string.IsNullOrEmpty(typeName)) return string.Empty;

        string root = EditorAssetCatalog.Instance.ContentRoot;
        //只缓存命中的结果：没找到的类型下次还要再找一遍，否则新建脚本文件后永远解析不到
        if (cachedPath.Length != 0
            && cachedManaged == isManaged
            && string.Equals(typeName, cachedTypeName, StringComparison.Ordinal)
            && string.Equals(root, cachedContentRoot, StringComparison.Ordinal))
            return cachedPath;

        string path = Resolve(typeName, root, isManaged);
        cachedTypeName = typeName;
        cachedManaged = isManaged;
        cachedContentRoot = root;
        cachedPath = path;
        return path;
    }

    /// <summary>在项目面板定位脚本文件；解析不到文件时返回 false。</summary>
    internal static bool Locate(string typeName, bool isManaged)
    {
        string path = FindSourcePath(typeName, isManaged);
        if (path.Length == 0) return false;
        ProjectPanel.Ping(EditorAssetCatalog.Instance.ToResourceKey(path));
        return true;
    }

    /// <summary>用系统默认程序打开脚本文件；解析不到文件时返回 false。</summary>
    internal static bool Edit(string typeName, bool isManaged)
    {
        string path = FindSourcePath(typeName, isManaged);
        if (path.Length == 0) return false;
        EditorAssetCatalog.OpenFile(path);
        return true;
    }

    //在内容根内定位脚本源文件
    private static string Resolve(string typeName, string root, bool isManaged)
    {
        string shortName = GetShortTypeName(typeName);
        if (shortName.Length == 0 || string.IsNullOrWhiteSpace(root) || !Directory.Exists(root)) return string.Empty;

        //扩展名按优先级排。C++ 先认头文件：MetaGen 只扫 .h，类的声明与字段都在里面
        string[] extensions = isManaged ? [".cs"] : [".h", ".cpp"];
        List<string> files = CollectSourceFiles(root, extensions);

        //文件名与类型名同名的先认：这是项目惯例，绝大多数脚本都命中
        foreach (string extension in extensions)
        {
            string? named = files.FirstOrDefault(file =>
                string.Equals(Path.GetFileNameWithoutExtension(file), shortName, StringComparison.Ordinal)
                && Path.GetExtension(file).Equals(extension, StringComparison.OrdinalIgnoreCase));
            if (named != null) return named;
        }

        //文件名对不上时退回扫类型声明，一个文件里放多个类的脚本靠这条兜住
        return files.FirstOrDefault(file => DeclaresType(file, shortName)) ?? string.Empty;
    }

    //收集内容根下指定扩展名的源文件。代码一律在内容根内——托管脚本见 Orbeden.Bindings.targets，
    //原生脚本见 Orbeden.Native.targets。目录规则与 EditorAssetCatalog 一致：跳过以点开头的目录。
    private static List<string> CollectSourceFiles(string root, string[] extensions)
    {
        List<string> result = [];
        Stack<string> pending = new();
        pending.Push(root);
        while (pending.Count > 0)
        {
            string directory = pending.Pop();
            try
            {
                foreach (string child in Directory.EnumerateDirectories(directory))
                {
                    if (Path.GetFileName(child).StartsWith('.')) continue;
                    pending.Push(child);
                }
                foreach (string file in Directory.EnumerateFiles(directory))
                {
                    if (extensions.Contains(Path.GetExtension(file), StringComparer.OrdinalIgnoreCase)) result.Add(file);
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                //读不了的目录跳过：定位失败好过让面板炸掉
            }
        }
        return result;
    }

    //判断文件里是否声明了指定短名的类型
    private static bool DeclaresType(string file, string shortName)
    {
        try
        {
            foreach (string line in File.ReadLines(file))
            {
                int index = line.IndexOf(shortName, StringComparison.Ordinal);
                if (index < 0) continue;
                //名字两侧必须是边界，否则找 PlayerController 会命中 PlayerControllerFactory
                if (index > 0 && !char.IsWhiteSpace(line[index - 1])) continue;
                int end = index + shortName.Length;
                if (end < line.Length && !char.IsWhiteSpace(line[end]) && line[end] is not ('<' or '{')) continue;

                string head = line[..index].TrimEnd();
                if (head.EndsWith("class", StringComparison.Ordinal)
                    || head.EndsWith("struct", StringComparison.Ordinal)
                    || head.EndsWith("record", StringComparison.Ordinal)
                    || head.EndsWith("interface", StringComparison.Ordinal))
                    return true;
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
        }
        return false;
    }

    //取类型名去掉命名空间与外层类后的短名。C++ 类型名是裸类名，这里原样返回
    private static string GetShortTypeName(string typeName)
    {
        int separator = Math.Max(typeName.LastIndexOf('.'), typeName.LastIndexOf('+'));
        return separator >= 0 ? typeName[(separator + 1)..] : typeName;
    }
}
