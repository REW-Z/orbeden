using System.Text.Json;
using System.Xml.Linq;
using Orbeden;

namespace OrbedenEditor;

internal enum ScriptBuildOperation : byte { Check, Begin, Complete }

/// <summary>索引脚本源文件、维护内容指纹并记录成功构建的输入。</summary>
internal static class EditorScriptFiles
{
#if DEBUG
    private const string NativeConfiguration = "Debug";
#else
    private const string NativeConfiguration = "Release";
#endif
    private static readonly HashSet<string> generatedDirectories = new(StringComparer.OrdinalIgnoreCase)
    {
        "Build", "ResourceCache", "Lib", "Legacy", "bin", "obj", "Content/RawContent",
    };
    private static readonly Dictionary<bool, (string Root, Dictionary<string, EditorAssetCache.Stamp> Inputs)> pendingBuilds = [];
    private static Dictionary<string, EditorAssetCache.Stamp> observedStamps = new(StringComparer.OrdinalIgnoreCase);
    private static string observedRoot = string.Empty;
    private static bool observationsChanged;

    /// <summary>识别托管与原生脚本源文件。</summary>
    internal static bool IsSource(string path) => Path.GetExtension(path).ToLowerInvariant() is
        ".cs" or ".cpp" or ".cc" or ".cxx" or ".c" or ".h" or ".hpp" or ".inl";

    /// <summary>检查构建输入，或在构建前后记录成功快照。</summary>
    internal static bool UpdateBuild(ScriptBuildOperation operation, bool native)
    {
        string root = EditorApplication.GetProjectText(EditorProjectField.Root);
        if (root.Length == 0)
        {
            return false;
        }

        if (native && !Directory.EnumerateFiles(root, "*.vcxproj").Any())
        {
            return false;
        }

        LoadObservedStamps(root);
        try
        {
            Dictionary<string, EditorAssetCache.Stamp> inputs = ReadBuildInputs(root, native);
            string cache = GetBuildStatusPath(root, native);
            switch (operation)
            {
                case ScriptBuildOperation.Check:
                    AddBuildOutputs(root, native, inputs);
                    Dictionary<string, EditorAssetCache.Stamp>? built = null;
                    try
                    {
                        if (File.Exists(cache))
                        {
                            built = JsonSerializer.Deserialize<Dictionary<string, EditorAssetCache.Stamp>>(File.ReadAllText(cache));
                        }
                    }
                    catch (JsonException)
                    {
                        //重建损坏的本机快照
                    }
                    return built == null || inputs.Values.Any(stamp => stamp.ContentHash == "-") || !SameContent(inputs, built);

                case ScriptBuildOperation.Begin:
                    pendingBuilds[native] = (root, inputs);
                    return true;

                case ScriptBuildOperation.Complete:
                    if (!pendingBuilds.Remove(native, out var pending) || pending.Root != root || !SameContent(inputs, pending.Inputs))
                    {
                        throw new IOException("Script inputs changed during the build. Refresh to retry.");
                    }
                    AddBuildOutputs(root, native, inputs);
                    if (inputs.Values.Any(stamp => stamp.ContentHash == "-"))
                    {
                        throw new IOException("Script build outputs are incomplete. Refresh to retry.");
                    }
                    Directory.CreateDirectory(Path.GetDirectoryName(cache)!);
                    WriteFingerprintFile(cache, JsonSerializer.Serialize(inputs) + "\n");
                    return true;

                default:
                    throw new ArgumentOutOfRangeException(nameof(operation));
            }
        }
        finally
        {
            SaveObservedStamps();
        }
    }

    //读取当前项目的文件观察记录
    private static void LoadObservedStamps(string root)
    {
        if (string.Equals(root, observedRoot, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }
        SaveObservedStamps();
        observedRoot = root;
        observedStamps.Clear();
        observationsChanged = false;
        string file = Path.Combine(root, "ResourceCache", "ScriptBuild", "Observed-v1.json");
        if (!File.Exists(file))
        {
            return;
        }
        try
        {
            Dictionary<string, EditorAssetCache.Stamp>? saved = JsonSerializer.Deserialize<Dictionary<string, EditorAssetCache.Stamp>>(File.ReadAllText(file));
            if (saved != null)
            {
                observedStamps = new(saved, StringComparer.OrdinalIgnoreCase);
            }
        }
        catch (JsonException)
        {
            //重新扫描损坏的观察记录
        }
    }

    //保存文件观察记录
    private static void SaveObservedStamps()
    {
        if (!observationsChanged || observedRoot.Length == 0)
        {
            return;
        }
        string directory = Path.Combine(observedRoot, "ResourceCache", "ScriptBuild");
        Directory.CreateDirectory(directory);
        WriteFingerprintFile(Path.Combine(directory, "Observed-v1.json"), JsonSerializer.Serialize(observedStamps) + "\n");
        observationsChanged = false;
    }

    //按修改时间筛选需要重新计算哈希的文件
    private static EditorAssetCache.Stamp ReadObservedStamp(string path)
    {
        path = Path.GetFullPath(path);
        FileInfo file = new(path);
        bool exists = file.Exists;
        long modified = exists ? file.LastWriteTimeUtc.Ticks : 0;
        if (observedStamps.TryGetValue(path, out EditorAssetCache.Stamp? previous) && previous != null
            && previous.Modified == modified && (previous.Length >= 0) == exists)
        {
            return previous;
        }
        EditorAssetCache.Stamp stamp = EditorAssetCache.ReadContentStamp(path, true);
        observedStamps[path] = stamp;
        observationsChanged = true;
        return stamp;
    }

    //收集项目源码、构建配置和 SDK 输入
    private static Dictionary<string, EditorAssetCache.Stamp> ReadBuildInputs(string root, bool native)
    {
        Dictionary<string, EditorAssetCache.Stamp> inputs = new(StringComparer.OrdinalIgnoreCase);
        foreach (string file in EnumerateBuildFiles(root))
        {
            string extension = Path.GetExtension(file).ToLowerInvariant();
            bool source = IsSource(file) && (native ? extension != ".cs" : extension == ".cs");
            bool configuration = extension is ".props" or ".targets" || extension == (native ? ".vcxproj" : ".csproj");
            if (!source && !configuration)
            {
                continue;
            }
            inputs[Path.GetFullPath(file)] = ReadObservedStamp(file);
        }

        //收集当前项目实际引用的 SDK 文件
        string sdkReference = Path.Combine(root, "Lib", "OrbedenSdk.path");
        string sdk;
        if (native)
        {
            sdk = Path.Combine(EditorApplication.GetProjectText(EditorProjectField.Repository), "OrbedenEditor", "Sdk");
        }
        else
        {
            inputs[sdkReference] = ReadObservedStamp(sdkReference);
            sdk = File.ReadAllText(sdkReference).Trim();
        }
        string[] directories = native ? ["Native/Include", "Tools/OrbedenMetaGen"] : ["Tools/OrbedenMetaGen"];
        foreach (string directory in directories)
        {
            foreach (string file in EnumerateBuildFiles(Path.Combine(sdk, directory)))
            {
                string extension = Path.GetExtension(file).ToLowerInvariant();
                if (IsSource(file) || extension is ".dll" or ".json" or ".props" or ".targets" or ".csproj")
                {
                    inputs[file] = ReadObservedStamp(file);
                }
            }
        }
        string[] sdkFiles = native
            ? ["Shared/GameModule.cpp", "Native/Orbeden.Native.props", "Native/Orbeden.Native.targets", "Native/Bindings.Manifest.json",
                "Native/WindowsX64/" + NativeConfiguration + "/OrbedenCore.lib"]
            : ["Shared/GameAotExports.cs", "Managed/OrbedenCore.CSharp/OrbedenCore.CSharp.dll", "Managed/Orbeden.Editor/Orbeden.Editor.dll"];
        foreach (string relative in sdkFiles)
        {
            string file = Path.GetFullPath(Path.Combine(sdk, relative));
            inputs[file] = ReadObservedStamp(file);
        }
        if (!native)
        {
            string runtime = Path.Combine(root, "Lib", "OrbedenCore.CSharp.dll");
            inputs[runtime] = ReadObservedStamp(runtime);
        }
        return inputs;
    }

    //枚举构建源文件并跳过生成目录与原始内容目录
    private static IEnumerable<string> EnumerateBuildFiles(string root)
    {
        if (!Directory.Exists(root))
        {
            yield break;
        }
        Stack<string> pending = new();
        pending.Push(root);
        while (pending.Count > 0)
        {
            string directory = pending.Pop();
            foreach (string child in Directory.EnumerateDirectories(directory))
            {
                string name = Path.GetFileName(child);
                string relative = Path.GetRelativePath(root, child).Replace('\\', '/');
                if (name.StartsWith('.') || generatedDirectories.Contains(relative)
                    || (File.GetAttributes(child) & FileAttributes.ReparsePoint) != 0)
                {
                    continue;
                }
                pending.Push(child);
            }
            foreach (string file in Directory.EnumerateFiles(directory).Order(StringComparer.Ordinal))
            {
                yield return Path.GetFullPath(file);
            }
        }
    }

    //将当前构建产物加入快照
    private static void AddBuildOutputs(string root, bool native, Dictionary<string, EditorAssetCache.Stamp> inputs)
    {
        string name = EditorApplication.GetProjectText(EditorProjectField.Name);
        if (native)
        {
            string output = Path.Combine(root, "Build", "Native", name + "Native.dll");
            inputs[output] = ReadObservedStamp(output);
            return;
        }

        string project = Path.Combine(root, name + ".csproj");
        if (!File.Exists(project))
        {
            project = Directory.EnumerateFiles(root, "*.csproj").Order(StringComparer.Ordinal).First();
        }
        string? assembly = XDocument.Load(project).Descendants().FirstOrDefault(element => element.Name.LocalName == "AssemblyName")?.Value;
        if (string.IsNullOrEmpty(assembly))
        {
            assembly = Path.GetFileNameWithoutExtension(project);
        }
        foreach (string suffix in new[] { ".dll", ".Editor.dll" })
        {
            string output = Path.Combine(root, "Build", "Managed", assembly + suffix);
            inputs[output] = ReadObservedStamp(output);
        }
    }

    //比较文件集合与内容指纹
    private static bool SameContent(Dictionary<string, EditorAssetCache.Stamp> current, Dictionary<string, EditorAssetCache.Stamp> previous)
    {
        return current.Count == previous.Count && current.All(pair => previous.TryGetValue(pair.Key, out EditorAssetCache.Stamp? stamp)
            && stamp != null && stamp.Length == pair.Value.Length && stamp.ContentHash == pair.Value.ContentHash);
    }

    //按构建语言与配置定位本机快照
    private static string GetBuildStatusPath(string root, bool native)
    {
        string target = native ? "Native-" + NativeConfiguration : "Managed-Debug";
        return Path.Combine(root, "ResourceCache", "ScriptBuild", target + "-v1.json");
    }

    //原子替换指纹文件
    private static void WriteFingerprintFile(string path, string json)
    {
        File.WriteAllText(path + ".tmp", json, InteropText.CreateUtf8Encoding(false));
        File.Move(path + ".tmp", path, true);
    }

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

    //收集内容根下指定扩展名的源文件。代码一律在内容根内——托管脚本见 Orbeden.Game.targets，
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
                    if (PathDefines.IsRawContentPath(Path.GetRelativePath(root, child))) continue;
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
