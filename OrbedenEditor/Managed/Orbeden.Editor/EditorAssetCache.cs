using System.Diagnostics;
using System.Text;
using System.Text.Json;
using Orbeden;

namespace OrbedenEditor;

/// <summary>串行后台导入和可重建资源数据库，UI 只读取轻量清单。
///
/// 每个源文件有一个伴生 `.resinfo`（与源文件同目录，随资源进版本管理），分两部分：
/// 导入设置（用户可编辑，重新导入时保留）与内部隐含资源清单（每次导入重新生成）。
/// 对象产物 `.orbo` 仍在可删除重建的 ResourceCache/Imported 下。 </summary>
internal static class EditorAssetCache
{
    internal sealed record Stamp(string Path, long Length, long Modified);
    internal sealed record Manifest(int Version, string Source, Dictionary<string, string> Settings,
        List<Stamp> Dependencies, List<string> Blobs, EditorAssetInspection.Result Data);
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

    private const string MetadataExtension = ".resinfo";
    private const string SettingsFileNameSuffix = ".import.settings";
    internal const int CurrentVersion = 3;

    /// <summary>将文件路径转换为 Content 相对路径。</summary>
    private static string GetRelativePath(string path) => Path.GetRelativePath(root, Path.GetFullPath(path)).Replace('\\', '/');

    /// <summary>读取依赖文件的相对路径、长度和修改时间。</summary>
    private static Stamp ReadStamp(string path)
    {
        FileInfo info = new(Path.GetFullPath(path));
        return new Stamp(GetRelativePath(info.FullName), info.Exists ? info.Length : -1, info.Exists ? info.LastWriteTimeUtc.Ticks : 0);
    }

    /// <summary>伴生文件：与源文件同目录，承载导入设置与清单。</summary>
    private static string GetMetadataPath(string path) => Path.GetFullPath(path) + MetadataExtension;

    /// <summary>对象产物目录：按 Content 目录结构存放在可重建的缓存下。</summary>
    private static string GetBlobDirectory(string path)
    {
        string relative = GetRelativePath(path);
        if (Path.IsPathRooted(relative) || relative == ".." || relative.StartsWith("../", StringComparison.Ordinal))
            throw new IOException("Asset source must stay inside Content.");
        return Path.Combine(Path.GetDirectoryName(root)!, "ResourceCache", "Imported", Path.GetDirectoryName(relative)!);
    }

    /// <summary>验证当前源文件独占的对象缓存名称。</summary>
    private static bool IsBlobName(string source, string? name)
    {
        string prefix = Path.GetFileName(source) + ".";
        return name != null && name.StartsWith(prefix, StringComparison.Ordinal) && name.EndsWith(".orbo", StringComparison.Ordinal)
            && name.Length == prefix.Length + 21 && name.AsSpan(prefix.Length, 16).ContainsAnyExcept("0123456789abcdef") == false;
    }

    /// <summary>删除单个源文件的对象产物与临时文件，保留伴生文件里的导入设置。</summary>
    private static void DeleteBlobs(string source)
    {
        string directory = GetBlobDirectory(source);
        if (!Directory.Exists(directory)) return;
        string settings = Path.Combine(directory, Path.GetFileName(source) + SettingsFileNameSuffix);
        File.Delete(settings);
        File.Delete(settings + ".tmp");
        foreach (string file in Directory.EnumerateFiles(directory))
            if (IsBlobName(source, Path.GetFileName(file))) File.Delete(file);
    }

    /// <summary>删除伴生文件与对象产物，源文件被移走或删除时使用。</summary>
    private static void DeleteAsset(string source)
    {
        DeleteBlobs(source);
        File.Delete(GetMetadataPath(source));
        File.Delete(GetMetadataPath(source) + ".tmp");
    }

    /// <summary>验证相对路径依赖及当前导入的全部对象产物。</summary>
    private static bool IsCurrent(Manifest value, string path)
    {
        try
        {
            return value.Version == CurrentVersion && value.Source == GetRelativePath(path)
                && value.Dependencies is { Count: > 0 } && value.Blobs != null && value.Data?.Objects != null
                && value.Dependencies.All(stamp => stamp != null && !string.IsNullOrEmpty(stamp.Path)
                    && !Path.IsPathRooted(stamp.Path) && ReadStamp(Path.Combine(root, stamp.Path)) == stamp)
                && value.Blobs.All(blob => IsBlobName(path, blob) && File.Exists(Path.Combine(GetBlobDirectory(path), blob)))
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

            //导入设置是用户数据，重新导入只重建清单部分，设置原样保留
            Manifest manifest = new(CurrentVersion, GetRelativePath(completed.Source), ReadSettings(completed.Source),
                stamps, blobs, new(objects, messages));
            if (!IsCurrent(manifest, completed.Source)) throw new IOException("Import cache is incomplete.");
            WriteMetadata(completed.Source, manifest);
            completed.Entry.Manifest = manifest;
            completed.Entry.Error = string.Empty;
            completed.Entry.NeedsImport = false;
            completed.Entry.MetadataStamp = ReadStamp(GetMetadataPath(completed.Source));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            completed.Entry.Manifest = null;
            completed.Entry.Error = exception.Message;
        }
        finally { completed.Process.Dispose(); }
    }

    /// <summary>读取伴生文件里的导入设置；没有伴生文件或读不出来时返回空表。</summary>
    internal static Dictionary<string, string> ReadSettings(string path)
    {
        string file = GetMetadataPath(path);
        if (!File.Exists(file)) return new(StringComparer.Ordinal);
        try
        {
            Manifest? manifest = JsonSerializer.Deserialize<Manifest>(File.ReadAllText(file));
            if (manifest?.Settings == null) return new(StringComparer.Ordinal);
            return new Dictionary<string, string>(manifest.Settings, StringComparer.Ordinal);
        }
        catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException)
        { return new(StringComparer.Ordinal); }
    }

    /// <summary>把 "源Key\t设置名\t值" 设置表编码给原生侧；原生按源文件 Key 逐行取用。</summary>
    internal static string EncodeSettingsTable(string path, Dictionary<string, string> settings)
    {
        StringBuilder text = new(AssetImportSettingsTableHeader);
        if (settings.Count == 0) return text.ToString();
        string source = GetRelativePath(path);
        foreach ((string name, string value) in settings)
            text.Append('\n').Append(source).Append('\t').Append(name).Append('\t').Append(value);
        return text.ToString();
    }

    private const string AssetImportSettingsTableHeader = "OrbedenImport1";

    /// <summary>把内容根下所有带导入设置的源文件编码成一张表，供批量 Reimport 使用。
    /// 只有真正改过设置的资源才会有条目，原生侧按源文件 Key 逐行取用。</summary>
    internal static string EncodeAllSettings()
    {
        StringBuilder text = new(AssetImportSettingsTableHeader);
        if (root.Length == 0 || !Directory.Exists(root)) return text.ToString();
        foreach (string metadata in Directory.EnumerateFiles(root, "*" + MetadataExtension,
            new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint }))
        {
            string source = metadata[..^MetadataExtension.Length];
            if (!File.Exists(source)) continue;
            foreach ((string name, string value) in ReadSettings(source))
                text.Append('\n').Append(GetRelativePath(source)).Append('\t').Append(name).Append('\t').Append(value);
        }
        return text.ToString();
    }

    /// <summary>原子写出伴生文件。</summary>
    private static void WriteMetadata(string path, Manifest manifest)
    {
        string file = GetMetadataPath(path);
        string temporary = file + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(manifest), new UTF8Encoding(false));
        File.Move(temporary, file, true);
    }

    /// <summary>保存导入设置并让缓存失效，下次读取时按新设置重新导入。</summary>
    internal static bool SaveSettings(string path, Dictionary<string, string> settings, out string error)
    {
        error = string.Empty;
        if (!EditorAssetsNative.CanModifyAssets()) { error = "Import settings cannot be changed while playing."; return false; }
        try
        {
            //伴生文件可能还没生成（资源尚未导入过），此时先写一份只有设置的最小文件，
            //清单部分会由紧接着的重新导入补上
            string file = GetMetadataPath(path);
            Manifest existing = ReadManifest(file) ?? new Manifest(CurrentVersion, GetRelativePath(path), [], [], [], new([], []));

            WriteMetadata(path, existing with { Settings = settings });
            Invalidate(force: true);
            EditorApplication.RequestRepaint();
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            error = exception.Message;
            return false;
        }
    }

    private static Manifest? ReadManifest(string file)
    {
        try
        {
            return File.Exists(file) ? JsonSerializer.Deserialize<Manifest>(File.ReadAllText(file)) : null;
        }
        catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException)
        { return null; }
    }

    /// <summary>请求清单：命中磁盘缓存立即返回，失效时排队导入而不阻塞 UI。</summary>
    internal static EditorAssetInspection.Result Get(string path)
    {
        if (root != Path.GetFullPath(PathDefines.ContentRoot).TrimEnd(Path.DirectorySeparatorChar)) Invalidate();
        Pump();
        if (!entries.TryGetValue(path, out Entry? entry)) entries[path] = entry = new Entry();
        string file = GetMetadataPath(path);
        Stamp metadataStamp = ReadStamp(file);
        if (entry.MetadataStamp != metadataStamp)
        {
            if (entry.Manifest != null || metadataStamp.Length >= 0) entry.Attempted = false;
            entry.MetadataStamp = metadataStamp;
            entry.Manifest = metadataStamp.Length >= 0 ? ReadManifest(file) : null;
            if (metadataStamp.Length >= 0 && entry.Manifest == null)
                entry.Error = "Import metadata is unreadable.";
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
                //只清对象产物，伴生文件里的导入设置要留给导入器使用
                DeleteBlobs(path);
                entry.MetadataStamp = null;
                string directory = GetBlobDirectory(path);
                Directory.CreateDirectory(directory);

                //把设置写成原生可解析的行表交给工作进程，避免在引擎里引入 JSON 解析
                string settingsFile = Path.Combine(directory, Path.GetFileName(path) + SettingsFileNameSuffix);
                File.WriteAllText(settingsFile, EncodeSettingsTable(path, ReadSettings(path)), new UTF8Encoding(false));

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
                start.ArgumentList.Add(settingsFile);
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

    /// <summary>清除源文件已移走或删除的记录、伴生文件与对象产物。</summary>
    internal static void EndScan()
    {
        foreach (string path in entries.Keys.Where(path => !sources.Contains(path)).ToArray()) entries.Remove(path);

        if (root.Length == 0 || !Directory.Exists(root)) return;
        foreach (string metadata in Directory.EnumerateFiles(root, "*" + MetadataExtension,
            new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint }))
        {
            //伴生文件与源文件同名同址，只多一个后缀
            string source = metadata[..^MetadataExtension.Length];
            if (!File.Exists(source)) DeleteAsset(source);
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
            Orbeden.Object? loaded = EditorAssetsNative.LoadCachedAsset(Path.Combine(GetBlobDirectory(path), asset.BlobName), key);
            if (loaded != null && expected.IsInstanceOfType(loaded)) result = loaded;
            return true;
        }
        return false;
    }
}
