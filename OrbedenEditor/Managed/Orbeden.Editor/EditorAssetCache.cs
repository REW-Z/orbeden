using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using Orbeden;

namespace OrbedenEditor;

/// <summary>串行后台导入和可重建资源数据库，UI 只读取轻量清单。</summary>
internal static class EditorAssetCache
{
    internal sealed record Stamp(string Path, long Length, long Modified);
    internal sealed record Manifest(int Version, string SourceId, Dictionary<string, JsonElement> ImportSettings, string Source, string Engine, string Generation,
        List<Stamp> Dependencies, EditorAssetInspection.Result Data)
    {
        [System.Text.Json.Serialization.JsonExtensionData]
        public Dictionary<string, JsonElement>? Extra { get; init; }
    }
    private sealed class Entry
    {
        internal Manifest? Manifest;
        internal bool Attempted;
        internal bool NeedsImport;
        internal bool MetadataValid = true;
        internal Stamp? MetadataStamp;
        internal string Error = string.Empty;
    }
    private sealed record Job(string Source, string Directory, Entry Entry, Process Process,
        Task<string> Output, Task<string> Error, DateTime Started, Stamp SourceStamp);
    private static readonly Dictionary<string, Entry> entries = new(StringComparer.OrdinalIgnoreCase);
    private static readonly HashSet<string> sources = new(StringComparer.OrdinalIgnoreCase);
    private static string root = string.Empty;
    private static Job? job;

    /// <summary>与引擎 StringId 共用 FNV-1a，源路径只决定缓存目录，不充当永久 GUID。</summary>
    private static string Hash(string key)
    {
        ulong hash = 14695981039346656037UL;
        foreach (byte value in Encoding.UTF8.GetBytes(key)) hash = unchecked((hash ^ value) * 1099511628211UL);
        return hash.ToString("x16", CultureInfo.InvariantCulture);
    }

    /// <summary>取得当前文件的失效标记，缺失文件也能参与依赖比较。</summary>
    private static Stamp ReadStamp(string path)
    {
        FileInfo info = new(path);
        return new Stamp(path, info.Exists ? info.Length : -1, info.Exists ? info.LastWriteTimeUtc.Ticks : 0);
    }

    /// <summary>构建产物变化时不复用旧版本导入器的结果。</summary>
    private static string EngineStamp() => File.GetLastWriteTimeUtc(Environment.ProcessPath!).Ticks + ":"
        + File.GetLastWriteTimeUtc(Path.Combine(Path.GetDirectoryName(Environment.ProcessPath!)!, "OrbedenCore.dll")).Ticks;

    /// <summary>复制伴随元数据，副本获得新身份，导入设置和未知扩展字段保留。</summary>
    internal static void CopyMetadata(string source, string destination, bool newIdentity = true)
    {
        string file = source + ".resinfo";
        if (!File.Exists(file)) return;
        var metadata = System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(file))?.AsObject()
            ?? throw new InvalidDataException("Invalid resource metadata: " + file);
        if (newIdentity) metadata["SourceId"] = Guid.NewGuid().ToString("N");
        metadata["Source"] = destination;
        metadata["Engine"] = string.Empty;
        metadata["Generation"] = string.Empty;
        metadata["Dependencies"] = new System.Text.Json.Nodes.JsonArray();
        metadata["Data"] = JsonSerializer.SerializeToNode(new EditorAssetInspection.Result([], []));
        File.WriteAllText(destination + ".resinfo", metadata.ToJsonString(), new UTF8Encoding(false));
    }
    /// <summary>按源路径选择 ResourceCache/Imported 下的数据库目录。</summary>
    private static string DirectoryFor(string path) => Path.Combine(Path.GetDirectoryName(root)!, "ResourceCache", "Imported",
        Hash(EditorAssetCatalog.Instance.ToResourceKey(path)));

    /// <summary>只验证轻量文件标记，不解析源文件内容。</summary>
    private static bool IsCurrent(Manifest value, string path) => value.Version == 1 && value.Source == path && value.Dependencies != null && value.Data?.Objects != null
        && Guid.TryParseExact(value.Generation, "N", out _)
        && value.Engine == EngineStamp() && value.Dependencies.Count != 0
        && value.Dependencies.All(stamp => ReadStamp(stamp.Path) == stamp)
        && value.Data.Objects.All(asset => asset.BlobName == Path.GetFileName(asset.BlobName)
            && File.Exists(Path.Combine(DirectoryFor(path), value.Generation, asset.BlobName)));

    /// <summary>编辑器关闭时结束自身启动的导入进程。</summary>
    internal static void Stop()
    {
        if (job == null) return;
        if (!job.Process.HasExited) job.Process.Kill(entireProcessTree: true);
        job.Process.Dispose();
        job = null;
    }
    /// <summary>目录变化后重新验证，切项目时终止本编辑器启动的工作进程。</summary>
    internal static void Invalidate(bool force = false)
    {
        string current = Path.GetFullPath(PathDefines.ContentRoot.Length == 0 ? "." : PathDefines.ContentRoot).TrimEnd(Path.DirectorySeparatorChar);
        if (root != current)
        {
            if (job != null)
            {
                if (!job.Process.HasExited) job.Process.Kill(entireProcessTree: true);
                job.Process.Dispose();
                job = null;
            }
            entries.Clear();
            sources.Clear();
            root = current;
        }
        foreach (Entry entry in entries.Values) { entry.Attempted = false; entry.Error = string.Empty; entry.NeedsImport |= force; }
    }

    /// <summary>收取独立导入进程的结果，成功后原子发布清单；失败保留旧版本。</summary>
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
            string[] values = File.ReadAllText(Path.Combine(completed.Directory, "inspection.result")).Split('\0');
            if (values.Length < 5 || (values.Length - 1) % 4 != 0 || values[^1].Length != 0)
                throw new IOException("Asset inspection output is incomplete.");
            List<EditorAssetInspection.Asset> objects = [];
            List<string> messages = [];
            HashSet<string> dependencies = new(StringComparer.OrdinalIgnoreCase) { completed.Source };
            for (int index = 0; index + 3 < values.Length; index += 4)
            {
                switch (values[index])
                {
                    case "object": objects.Add(new(values[index + 2], values[index + 3], values[index + 1], [])); break;
                    case "field" when objects.Count != 0: objects[^1].Fields.Add(new(values[index + 1], values[index + 2], values[index + 3])); break;
                    case "message": messages.Add(values[index + 1]); break;
                    case "error": throw new IOException(values[index + 1]);
                    case "dependency": dependencies.Add(Path.GetFullPath(Path.IsPathRooted(values[index + 1])
                        ? values[index + 1] : Path.Combine(root, values[index + 1]))); break;
                }
            }
            List<Stamp> stamps = dependencies.Select(ReadStamp).ToList();
            if (ReadStamp(completed.Source) != completed.SourceStamp
                || stamps.Any(stamp => stamp.Modified > completed.Started.Ticks))
                throw new IOException("Source or dependency changed during import. Refresh to retry.");
            Manifest? previous = completed.Entry.Manifest;
            string sidecar = completed.Source + ".resinfo";
            if (File.Exists(sidecar)) previous = JsonSerializer.Deserialize<Manifest>(File.ReadAllText(sidecar));
            if (previous != null && previous.Version != 1) throw new IOException("Unsupported resinfo version; metadata was preserved.");
            Manifest manifest = new(1, previous?.SourceId ?? Guid.NewGuid().ToString("N"), previous?.ImportSettings ?? [],
                completed.Source, EngineStamp(), Path.GetFileName(completed.Directory), stamps, new(objects, messages)) { Extra = previous?.Extra };
            if (!IsCurrent(manifest, completed.Source)) throw new IOException("Import cache is incomplete.");
            string path = completed.Source + ".resinfo";
            string temporary = path + ".tmp";
            File.WriteAllText(temporary, JsonSerializer.Serialize(manifest), new UTF8Encoding(false));
            File.Move(temporary, path, true);
            completed.Entry.Manifest = manifest;
            completed.Entry.Error = string.Empty;
            completed.Entry.NeedsImport = false;
            completed.Entry.MetadataValid = true;
            completed.Entry.MetadataStamp = ReadStamp(path);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
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
        string file = path + ".resinfo";
        Stamp metadataStamp = ReadStamp(file);
        if (entry.MetadataStamp != metadataStamp)
        {
            entry.MetadataStamp = metadataStamp;
            entry.MetadataValid = true;
            try
            {
                entry.Manifest = File.Exists(file) ? JsonSerializer.Deserialize<Manifest>(File.ReadAllText(file)) : null;
                if (File.Exists(file) && (entry.Manifest == null || entry.Manifest.Version != 1
                    || !Guid.TryParseExact(entry.Manifest.SourceId, "N", out _)))
                    throw new IOException("Invalid or unsupported resinfo metadata; the file has been preserved.");
            }
            catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException)
            { entry.Error = exception.Message; entry.Attempted = true; entry.MetadataValid = false; }
        }
        if (entry.MetadataValid && !entry.NeedsImport && entry.Manifest != null && IsCurrent(entry.Manifest, path)) return entry.Manifest.Data;
        if (!entry.MetadataValid) return new(entry.Manifest?.Data?.Objects ?? [], [entry.Error], true);
        if (!entry.Attempted && job == null)
        {
            entry.Attempted = true;
            try
            {
                string directory = Path.Combine(DirectoryFor(path), Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(directory);
                ProcessStartInfo start = new(Environment.ProcessPath!)
                {
                    UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
                    RedirectStandardOutput = true, RedirectStandardError = true,
                };
                start.ArgumentList.Add("--inspect-asset");
                start.ArgumentList.Add(root);
                start.ArgumentList.Add(EditorAssetCatalog.Instance.ToResourceKey(path));
                start.ArgumentList.Add(directory);
                DateTime began = DateTime.UtcNow;
                Stamp stamp = ReadStamp(path);
                Process process = Process.Start(start) ?? throw new IOException("Could not start asset importer.");
                job = new(path, directory, entry, process, process.StandardOutput.ReadToEndAsync(), process.StandardError.ReadToEndAsync(), began, stamp);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception)
            { entry.Error = exception.Message; }
        }
        bool pending = !entry.Attempted || job?.Entry == entry;
        if (pending) EditorApplication.RequestRepaint();
        string message = pending ? "Importing resource metadata..." : "Import failed: " + entry.Error;
        return new(entry.Manifest?.Data.Objects ?? [], [message + (entry.Manifest == null ? "" : " (showing previous import)")], true, pending);
    }

    /// <summary>目录扫描只登记待索引文件，不在扫描阶段解析复合资源。</summary>
    internal static void BeginScan() => sources.Clear();

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
        foreach (Entry entry in entries.Values)
        {
            if (!entry.MetadataValid || entry.NeedsImport || entry.Manifest == null || !IsCurrent(entry.Manifest, entry.Manifest.Source)) continue;
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
        foreach (Entry entry in entries.Values)
        {
            Manifest? manifest = entry.Manifest;
            EditorAssetInspection.Asset? asset = manifest?.Data.Objects.FirstOrDefault(asset => asset.Key == key);
            if (manifest == null || asset == null) continue;
            if (!entry.MetadataValid || entry.NeedsImport || !IsCurrent(manifest, manifest.Source)) return true;
            Orbeden.Object? loaded = EditorAssetsNative.LoadCachedAsset(Path.Combine(DirectoryFor(manifest.Source), manifest.Generation, asset.BlobName), key);
            if (loaded != null && expected.IsInstanceOfType(loaded)) result = loaded;
            return true;
        }
        return false;
    }
}
