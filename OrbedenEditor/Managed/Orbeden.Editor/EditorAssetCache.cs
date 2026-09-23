using System.Diagnostics;
using System.Text;
using System.Text.Json;
using Orbeden;

namespace OrbedenEditor;

/// <summary>串行后台导入和可重建资源数据库，UI 只读取轻量清单。</summary>
internal static class EditorAssetCache
{
    internal sealed record Stamp(string Path, long Length, long Modified);
    internal sealed record Manifest(int Version, string Source, List<Stamp> Dependencies,
        List<string> Blobs, EditorAssetInspection.Result Data);
    private sealed class Entry
    {
        internal Manifest? Manifest;
        internal bool Attempted;
        internal bool NeedsImport;
        internal Stamp? MetadataStamp;
        internal string Error = string.Empty;
    }
    private sealed record Job(string Source, Entry Entry, Process Process,
        Task<string> Output, Task<string> Error, DateTime Started, Stamp SourceStamp);
    private static readonly Dictionary<string, Entry> entries = new(StringComparer.OrdinalIgnoreCase);
    private static readonly HashSet<string> sources = new(StringComparer.OrdinalIgnoreCase);
    private static string root = string.Empty;
    private static Job? job;

    /// <summary>将文件路径转换为 Content 相对路径。</summary>
    private static string GetRelativePath(string path) => Path.GetRelativePath(root, Path.GetFullPath(path)).Replace('\\', '/');

    /// <summary>读取依赖文件的相对路径、长度和修改时间。</summary>
    private static Stamp ReadStamp(string path)
    {
        FileInfo info = new(Path.GetFullPath(path));
        return new Stamp(GetRelativePath(info.FullName), info.Exists ? info.Length : -1, info.Exists ? info.LastWriteTimeUtc.Ticks : 0);
    }

    /// <summary>按 Content 目录结构定位 Imported 中的源文件缓存前缀。</summary>
    private static string GetCachePath(string path)
    {
        string relative = GetRelativePath(path);
        if (Path.IsPathRooted(relative) || relative == ".." || relative.StartsWith("../", StringComparison.Ordinal))
            throw new IOException("Asset source must stay inside Content.");
        return Path.Combine(Path.GetDirectoryName(root)!, "ResourceCache", "Imported", relative);
    }

    /// <summary>验证当前源文件独占的对象缓存名称。</summary>
    private static bool IsBlobName(string source, string? name)
    {
        string prefix = Path.GetFileName(source) + ".";
        return name != null && name.StartsWith(prefix, StringComparison.Ordinal) && name.EndsWith(".orbo", StringComparison.Ordinal)
            && name.Length == prefix.Length + 21 && name.AsSpan(prefix.Length, 16).ContainsAnyExcept("0123456789abcdef") == false;
    }

    /// <summary>删除单个源文件的缓存清单及对象，不触碰同目录其他资源。</summary>
    private static void DeleteCache(string source)
    {
        string cache = GetCachePath(source);
        string directory = Path.GetDirectoryName(cache)!;
        if (!Directory.Exists(directory)) return;
        File.Delete(cache + ".resinfo");
        File.Delete(cache + ".resinfo.tmp");
        foreach (string file in Directory.EnumerateFiles(directory))
            if (IsBlobName(source, Path.GetFileName(file))) File.Delete(file);
    }

    /// <summary>验证相对路径依赖及当前导入的全部对象产物。</summary>
    private static bool IsCurrent(Manifest value, string path)
    {
        try
        {
            return value.Version == 2 && value.Source == GetRelativePath(path)
                && value.Dependencies is { Count: > 0 } && value.Blobs != null && value.Data?.Objects != null
                && value.Dependencies.All(stamp => stamp != null && !string.IsNullOrEmpty(stamp.Path)
                    && !Path.IsPathRooted(stamp.Path) && ReadStamp(Path.Combine(root, stamp.Path)) == stamp)
                && value.Blobs.All(blob => IsBlobName(path, blob) && File.Exists(Path.Combine(Path.GetDirectoryName(GetCachePath(path))!, blob)))
                && value.Data.Objects.All(asset => asset != null && value.Blobs.Contains(asset.BlobName));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        { return false; }
    }

    /// <summary>编辑器关闭时结束自身启动的导入进程。</summary>
    internal static void Stop()
    {
        if (job == null) return;
        if (!job.Process.HasExited) job.Process.Kill(entireProcessTree: true);
        job.Process.WaitForExit();
        job.Entry.Attempted = false;
        job.Process.Dispose();
        job = null;
    }
    /// <summary>目录变化后重新验证，切项目时终止本编辑器启动的工作进程。</summary>
    internal static void Invalidate(bool force = false)
    {
        Stop();
        string current = Path.GetFullPath(PathDefines.ContentRoot.Length == 0 ? "." : PathDefines.ContentRoot).TrimEnd(Path.DirectorySeparatorChar);
        if (root != current)
        {
            entries.Clear();
            sources.Clear();
            root = current;
        }
        foreach (Entry entry in entries.Values) { entry.Attempted = false; entry.Error = string.Empty; entry.NeedsImport |= force; }
    }

    /// <summary>收取独立导入进程的结果，成功后发布当前清单；失败不复用旧缓存。</summary>
    private static void Pump()
    {
        if (job == null) return;
        if (!job.Process.HasExited) { EditorApplication.RequestRepaint(); return; }
        Job completed = job;
        job = null;
        try
        {
            string output = completed.Output.GetAwaiter().GetResult();
            string error = completed.Error.GetAwaiter().GetResult();
            if (completed.Process.ExitCode != 0) throw new IOException("Asset importer failed. " + error + output);
            string[] values = output.Split('\0');
            if (values.Length < 5 || (values.Length - 1) % 4 != 0 || values[^1].Length != 0)
                throw new IOException("Asset inspection output is incomplete.");
            List<EditorAssetInspection.Asset> objects = [];
            List<string> messages = [];
            List<string> blobs = [];
            HashSet<string> dependencies = new(StringComparer.OrdinalIgnoreCase) { completed.Source };
            for (int index = 0; index + 3 < values.Length; index += 4)
            {
                switch (values[index])
                {
                    case "blob": blobs.Add(values[index + 1]); break;
                    case "object": objects.Add(new(values[index + 2], values[index + 3], values[index + 1], [])); break;
                    case "field" when objects.Count != 0: objects[^1].Fields.Add(new(values[index + 1], values[index + 2], values[index + 3])); break;
                    case "message": messages.Add(values[index + 1]); break;
                    case "error": throw new IOException(values[index + 1]);
                    case "dependency": dependencies.Add(Path.GetFullPath(Path.IsPathRooted(values[index + 1])
                        ? values[index + 1] : Path.Combine(root, values[index + 1]))); break;
                    default: throw new IOException("Unknown asset inspection record.");
                }
            }
            List<Stamp> stamps = dependencies.Select(ReadStamp).ToList();
            if (ReadStamp(completed.Source) != completed.SourceStamp
                || stamps.Any(stamp => stamp.Modified > completed.Started.Ticks))
                throw new IOException("Source or dependency changed during import. Refresh to retry.");
            Manifest manifest = new(2, GetRelativePath(completed.Source), stamps, blobs, new(objects, messages));
            if (!IsCurrent(manifest, completed.Source)) throw new IOException("Import cache is incomplete.");
            string path = GetCachePath(completed.Source) + ".resinfo";
            string temporary = path + ".tmp";
            File.WriteAllText(temporary, JsonSerializer.Serialize(manifest), new UTF8Encoding(false));
            File.Move(temporary, path, true);
            completed.Entry.Manifest = manifest;
            completed.Entry.Error = string.Empty;
            completed.Entry.NeedsImport = false;
            completed.Entry.MetadataStamp = ReadStamp(path);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            completed.Entry.Manifest = null;
            completed.Entry.Error = exception.Message;
        }
        finally { completed.Process.Dispose(); }
    }

    /// <summary>请求清单：命中磁盘缓存立即返回，失效时排队导入而不阻塞 UI。</summary>
    internal static EditorAssetInspection.Result Get(string path)
    {
        if (root != Path.GetFullPath(PathDefines.ContentRoot).TrimEnd(Path.DirectorySeparatorChar)) Invalidate();
        Pump();
        if (!entries.TryGetValue(path, out Entry? entry)) entries[path] = entry = new Entry();
        string file = GetCachePath(path) + ".resinfo";
        Stamp metadataStamp = ReadStamp(file);
        if (entry.MetadataStamp != metadataStamp)
        {
            if (entry.Manifest != null || metadataStamp.Length >= 0) entry.Attempted = false;
            entry.MetadataStamp = metadataStamp;
            try
            {
                entry.Manifest = File.Exists(file) ? JsonSerializer.Deserialize<Manifest>(File.ReadAllText(file)) : null;
            }
            catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException)
            { entry.Manifest = null; entry.Error = exception.Message; }
        }
        if (!entry.NeedsImport && entry.Manifest != null && IsCurrent(entry.Manifest, path)) return entry.Manifest.Data;
        if (entry.Manifest != null)
        {
            entry.Manifest = null;
            entry.Attempted = false;
        }
        if (!entry.Attempted && job == null)
        {
            entry.Attempted = true;
            try
            {
                entry.Manifest = null;
                entry.NeedsImport = true;
                DeleteCache(path);
                entry.MetadataStamp = null;
                string directory = Path.GetDirectoryName(GetCachePath(path))!;
                Directory.CreateDirectory(directory);
                ProcessStartInfo start = new(Environment.ProcessPath!)
                {
                    UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
                    RedirectStandardOutput = true, RedirectStandardError = true,
                    StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8,
                };
                start.ArgumentList.Add("--inspect-asset");
                start.ArgumentList.Add(root);
                start.ArgumentList.Add(EditorAssetCatalog.Instance.ToResourceKey(path));
                start.ArgumentList.Add(directory);
                DateTime began = DateTime.UtcNow;
                Stamp stamp = ReadStamp(path);
                Process process = Process.Start(start) ?? throw new IOException("Could not start asset importer.");
                job = new(path, entry, process, process.StandardOutput.ReadToEndAsync(), process.StandardError.ReadToEndAsync(), began, stamp);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception)
            { entry.Error = exception.Message; }
        }
        bool pending = !entry.Attempted || job?.Entry == entry;
        if (pending) EditorApplication.RequestRepaint();
        string message = pending ? "Importing resource metadata..." : "Import failed: " + entry.Error;
        return new([], [message], true, pending);
    }

    /// <summary>目录扫描只登记待索引文件，不在扫描阶段解析复合资源。</summary>
    internal static void BeginScan() => sources.Clear();

    /// <summary>清除源文件已移走或删除的缓存记录。</summary>
    internal static void EndScan()
    {
        foreach (string path in entries.Keys.Where(path => !sources.Contains(path)).ToArray()) entries.Remove(path);
        string imported = Path.Combine(Path.GetDirectoryName(root)!, "ResourceCache", "Imported");
        if (!Directory.Exists(imported)) return;
        foreach (string metadata in Directory.EnumerateFiles(imported, "*.resinfo", new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint }))
        {
            string source = Path.Combine(root, Path.GetRelativePath(imported, metadata)[..^8]);
            if (!File.Exists(source)) DeleteCache(source);
        }
    }

    /// <summary>把支持的源文件加入串行后台索引队列。</summary>
    internal static void Queue(string path) => sources.Add(path);

    /// <summary>每帧推进一个工作进程，保证未展开的资源也能进入 ObjectField 候选列表。</summary>
    internal static void Update()
    {
        if (PathDefines.ContentRoot.Length == 0) return;
        Pump();
        if (job != null) return;
        foreach (string path in sources)
        {
            if (!File.Exists(path)) continue;
            Get(path);
            if (job != null) return;
        }
    }
    /// <summary>把实际清单中的类型化资源提供给 ObjectField。</summary>
    internal static IEnumerable<ObjectFieldOption> GetOptions(Type expected)
    {
        foreach ((string path, Entry entry) in entries)
        {
            if (entry.NeedsImport || entry.Manifest == null || !IsCurrent(entry.Manifest, path)) continue;
            foreach (EditorAssetInspection.Asset asset in entry.Manifest.Data.Objects)
            {
                Type? actual = NativeBindingRuntime.GetManagedType(asset.TypeName) ?? typeof(Orbeden.Object).Assembly.GetType("Orbeden." + asset.TypeName);
                if (actual != null && expected.IsAssignableFrom(actual)) yield return new(asset.Key, asset.Key);
            }
        }
    }

    /// <summary>加载清单对应的单个 .orbo，独立于 Project 清单读取。</summary>
    internal static bool TryLoad(string key, Type expected, out Orbeden.Object? result)
    {
        result = null;
        foreach ((string path, Entry entry) in entries)
        {
            Manifest? manifest = entry.Manifest;
            EditorAssetInspection.Asset? asset = manifest?.Data?.Objects?.FirstOrDefault(asset => asset.Key == key);
            if (manifest == null || asset == null) continue;
            if (entry.NeedsImport || !IsCurrent(manifest, path)) return true;
            Orbeden.Object? loaded = EditorAssetsNative.LoadCachedAsset(Path.Combine(Path.GetDirectoryName(GetCachePath(path))!, asset.BlobName), key);
            if (loaded != null && expected.IsInstanceOfType(loaded)) result = loaded;
            return true;
        }
        return false;
    }
}
