using System.Diagnostics;
using System.Formats.Tar;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

internal static partial class Program
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    /// <summary>执行依赖还原或构建命令。</summary>
    private static async Task<int> Main(string[] args)
    {
        try
        {
            if (args.Length == 0 || args[0] == "--help")
            {
                Console.WriteLine("restore [--offline]\nbuild [--configuration Debug|Release] [--vs-install <directory>] [--offline]");
                return args.Length == 0 ? 1 : 0;
            }
            if (args[0] is not ("restore" or "build"))
                throw new ArgumentException($"Unknown command: {args[0]}");
            bool offline = false;
            string configuration = "Debug";
            string? vsInstall = null;
            for (int index = 1; index < args.Length; index++)
            {
                switch (args[index])
                {
                    case "--offline": offline = true; break;
                    case "--configuration" when index + 1 < args.Length: configuration = args[++index]; break;
                    case "--vs-install" when index + 1 < args.Length: vsInstall = args[++index]; break;
                    default: throw new ArgumentException($"Unknown or incomplete option: {args[index]}");
                }
            }
            if (configuration is not ("Debug" or "Release"))
                throw new ArgumentException("Configuration must be Debug or Release.");

            //查找依赖锁文件
            var directory = new DirectoryInfo(AppContext.BaseDirectory);
            while (directory != null && !File.Exists(Path.Combine(directory.FullName, "dependencies.lock.json")))
                directory = directory.Parent;
            string root = directory?.FullName ?? throw new FileNotFoundException("Dependency lock file not found.");
            JsonObject dependencyLock = JsonNode.Parse(File.ReadAllText(Path.Combine(root, "dependencies.lock.json")))!.AsObject();
            ValidateDependencyLock(root, dependencyLock);

            await RestorePackages(root, dependencyLock, offline);
            Console.WriteLine("Third-party dependencies restored and verified.");
            if (args[0] == "build")
                await BuildLibraries(root, configuration, vsInstall);
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Third-party packages: {error.Message}");
            return 1;
        }
    }

    /// <summary>校验锁定来源及受版本管理的许可快照。</summary>
    private static void ValidateDependencyLock(string root, JsonObject dependencyLock)
    {
        if (dependencyLock["lockVersion"]!.GetValue<int>() != 1)
            throw new InvalidDataException("Unsupported dependency lock version.");
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (JsonObject package in dependencyLock["packages"]!.AsArray().Select(value => value!.AsObject()))
        {
            string name = package["name"]!.GetValue<string>();
            string checksum = package["sha256"]!.GetValue<string>();
            if (name.Length == 0 || name.Any(character => !char.IsAsciiLetterLower(character) && !char.IsAsciiDigit(character) && character != '-') || !names.Add(name))
                throw new InvalidDataException($"Invalid or duplicate package name: {name}");
            if (checksum.Length != 64 || checksum.Any(character => !char.IsAsciiHexDigit(character) || char.IsUpper(character)))
                throw new InvalidDataException($"Invalid SHA-256: {name}");
            if (!Uri.TryCreate(package["url"]!.GetValue<string>(), UriKind.Absolute, out Uri? uri) || uri.Scheme != "https" || package["format"]!.GetValue<string>() is not ("zip" or "tar.gz" or "file"))
                throw new InvalidDataException($"Invalid package source: {name}");
            GetContainedPath(Path.Combine(root, "vendor"), name);
            GetContainedPath(Path.Combine(root, "vendor", name), package["subdirectory"]!.GetValue<string>());
        }
        string licenses = Path.Combine(root, "licenses");
        foreach (var snapshot in dependencyLock["licenseSnapshots"]!.AsObject())
        {
            if (HashFile(GetContainedPath(licenses, snapshot.Key)) != snapshot.Value!.GetValue<string>())
                throw new InvalidDataException($"License snapshot checksum mismatch: {snapshot.Key}");
        }
    }

    /// <summary>还原锁定依赖并发布宿主文件和字体。</summary>
    private static async Task RestorePackages(string root, JsonObject dependencyLock, bool offline)
    {
        string cache = Path.Combine(root, ".cache");
        string vendor = Path.Combine(root, "vendor");
        Directory.CreateDirectory(cache);
        Directory.CreateDirectory(vendor);
        using var client = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("OrbedenThirdParty/1");
        foreach (JsonObject package in dependencyLock["packages"]!.AsArray().Select(value => value!.AsObject()))
        {
            string name = package["name"]!.GetValue<string>();
            string destination = GetContainedPath(vendor, name);
            string statePath = Path.Combine(destination, ".orbeden-package.json");
            JsonObject? state = File.Exists(statePath) ? JsonNode.Parse(File.ReadAllText(statePath))!.AsObject() : null;
            if (state == null || !JsonNode.DeepEquals(state["package"], package) || !CheckRestoredFiles(destination, state))
            {
                string archive = await DownloadPackage(client, package, cache, offline);
                string stage = Path.Combine(cache, "stage-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(stage);
                try
                {
                    //解压并替换工作副本
                    ExtractPackage(archive, package, stage);
                    string extracted = GetContainedPath(stage, package["subdirectory"]!.GetValue<string>());
                    if (!Directory.Exists(extracted))
                        throw new DirectoryNotFoundException($"Missing package subtree: {name}");
                    JsonObject files = ReadFileHashes(extracted);
                    RemoveDirectory(destination, vendor);
                    Directory.CreateDirectory(destination);
                    foreach (var file in files)
                        CopyFile(GetContainedPath(extracted, file.Key), GetContainedPath(destination, file.Key));
                    WriteState(statePath, new JsonObject { ["package"] = package.DeepClone(), ["files"] = files });
                }
                finally
                {
                    RemoveDirectory(stage, cache);
                }
                Console.WriteLine($"Restored {name}");
            }

            //校验上游许可并发布原生宿主
            foreach (JsonObject license in package["licenseFiles"]!.AsArray().Select(value => value!.AsObject()))
            {
                string expected = license["sha256"]!.GetValue<string>();
                if (HashFile(GetContainedPath(Path.Combine(root, "licenses", name), license["file"]!.GetValue<string>())) != expected || HashFile(GetContainedPath(destination, license["source"]!.GetValue<string>())) != expected)
                    throw new InvalidDataException($"License snapshot mismatch: {name}");
            }
            string repository = Path.GetFullPath(Path.Combine(root, "..", ".."));
            if (name.StartsWith("dotnet-win-", StringComparison.Ordinal))
            {
                string arch = name["dotnet-".Length..];
                string native = Path.Combine(destination, "runtimes", arch, "native");
                string publish = Path.Combine(repository, "OrbedenEditor", "Src", "ThirdParty", "dotnet");
                foreach (string pattern in new[] { "*.h", "*.lib", "*.dll" })
                {
                    foreach (string file in Directory.EnumerateFiles(native, pattern))
                        CopyFile(file, Path.Combine(pattern == "*.h" ? Path.Combine(publish, "include") : Path.Combine(publish, "lib", arch), Path.GetFileName(file)));
                }
            }
            if (name == "noto-font")
                CopyFile(Path.Combine(destination, "Default.otf"), Path.Combine(repository, "OrbedenEditor", "Templates", "Builtin", "Fonts", "Default.otf"));
        }

        //运行上游 GLAD 生成器
        string glad = Path.Combine(vendor, "glad");
        JsonObject options = dependencyLock["glad"]!.AsObject();
        var generatorPackages = new JsonArray();
        foreach (JsonObject package in dependencyLock["packages"]!.AsArray().Select(value => value!.AsObject()))
        {
            if (package["name"]!.GetValue<string>() is "glad-generator" or "jinja2" or "markupsafe" or "python-runtime")
                generatorPackages.Add(package["sha256"]!.DeepClone());
        }
        var generation = new JsonObject { ["options"] = options.DeepClone(), ["packages"] = generatorPackages };
        string gladStatePath = Path.Combine(glad, ".orbeden-glad.json");
        JsonObject? gladState = File.Exists(gladStatePath) ? JsonNode.Parse(File.ReadAllText(gladStatePath))!.AsObject() : null;
        if (gladState == null || !JsonNode.DeepEquals(gladState["generation"], generation) || !CheckRestoredFiles(glad, gladState))
        {
            RemoveDirectory(glad, vendor);
            string python = Path.Combine(vendor, "python-runtime", "python.exe");
            var arguments = new List<string>
            {
                "-B", "-c", "import runpy,sys;sys.path[:0]=sys.argv[1:4];sys.argv=sys.argv[4:];runpy.run_module('glad',run_name='__main__')",
                Path.Combine(vendor, "glad-generator"), Path.Combine(vendor, "jinja2", "src"), Path.Combine(vendor, "markupsafe", "src"),
                "glad", "--api", options["api"]!.GetValue<string>(), "--extensions", string.Join(",", options["extensions"]!.AsArray().Select(value => value!.GetValue<string>())), "--out-path", glad
            };
            if (options["reproducible"]!.GetValue<bool>()) arguments.Add("--reproducible");
            arguments.Add("c");
            if (options["loader"]!.GetValue<bool>()) arguments.Add("--loader");
            if (await RunProcess(python, root, arguments) != 0)
                throw new InvalidOperationException("GLAD generation failed.");
            WriteState(gladStatePath, new JsonObject { ["generation"] = generation, ["files"] = ReadFileHashes(glad) });
        }
    }

    /// <summary>下载并校验固定归档。</summary>
    private static async Task<string> DownloadPackage(HttpClient client, JsonObject package, string cache, bool offline)
    {
        string name = package["name"]!.GetValue<string>();
        string checksum = package["sha256"]!.GetValue<string>();
        string suffix = package["format"]!.GetValue<string>() switch { "zip" => ".zip", "tar.gz" => ".tar.gz", _ => ".asset" };
        string archive = Path.Combine(cache, checksum + suffix);
        if (File.Exists(archive))
        {
            if (HashFile(archive) != checksum)
                throw new InvalidDataException($"Cached archive checksum mismatch: {name}");
            return archive;
        }
        if (offline)
            throw new FileNotFoundException($"Missing cached archive: {name}");
        Console.WriteLine($"Download {name} {package["version"]}");
        string temporary = archive + ".download";
        try
        {
            using HttpResponseMessage response = await client.GetAsync(package["url"]!.GetValue<string>(), HttpCompletionOption.ResponseHeadersRead);
            response.EnsureSuccessStatusCode();
            await using (Stream input = await response.Content.ReadAsStreamAsync())
            await using (FileStream output = File.Create(temporary))
                await input.CopyToAsync(output);
            if (HashFile(temporary) != checksum)
                throw new InvalidDataException($"Downloaded archive checksum mismatch: {name}");
            File.Move(temporary, archive, overwrite: true);
        }
        finally
        {
            File.Delete(temporary);
        }
        return archive;
    }

    /// <summary>解压归档并校验条目路径。</summary>
    private static void ExtractPackage(string archive, JsonObject package, string destination)
    {
        string format = package["format"]!.GetValue<string>();
        if (format == "file")
        {
            CopyFile(archive, GetContainedPath(destination, package["fileName"]!.GetValue<string>()));
            return;
        }
        bool stripRoot = package["stripRoot"]!.GetValue<bool>();
        string? archiveRoot = null;
        /// <summary>写入校验后的归档条目。</summary>
        void ExtractEntry(string name, bool directory, Stream? data)
        {
            GetContainedPath(destination, name);
            string relative = name.Replace('\\', '/');
            if (stripRoot)
            {
                int separator = relative.IndexOf('/');
                string entryRoot = separator < 0 ? relative : relative[..separator];
                archiveRoot ??= entryRoot;
                if (archiveRoot != entryRoot)
                    throw new InvalidDataException("Expected one archive root.");
                relative = separator < 0 ? "" : relative[(separator + 1)..];
            }
            string target = GetContainedPath(destination, relative);
            if (package["include"] is JsonArray includes && !includes.Any(value => relative == value!.GetValue<string>().TrimEnd('/') || value!.GetValue<string>().EndsWith('/') && relative.StartsWith(value.GetValue<string>(), StringComparison.Ordinal)))
                return;
            if (directory)
                Directory.CreateDirectory(target);
            else
            {
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                using FileStream output = File.Create(target);
                data?.CopyTo(output);
            }
        }
        if (format == "zip")
        {
            using ZipArchive source = ZipFile.OpenRead(archive);
            foreach (ZipArchiveEntry entry in source.Entries)
            {
                GetContainedPath(destination, entry.FullName);
                if ((entry.ExternalAttributes >> 16 & 0xF000) == 0xA000)
                    continue;
                using Stream data = entry.Open();
                ExtractEntry(entry.FullName, entry.FullName.EndsWith('/'), data);
            }
        }
        else
        {
            using FileStream file = File.OpenRead(archive);
            using var gzip = new GZipStream(file, CompressionMode.Decompress);
            using var source = new TarReader(gzip);
            while (source.GetNextEntry() is TarEntry entry)
            {
                if (entry.EntryType is not (TarEntryType.Directory or TarEntryType.RegularFile or TarEntryType.V7RegularFile))
                    throw new InvalidDataException($"Unsupported archive entry: {entry.Name}");
                ExtractEntry(entry.Name, entry.EntryType == TarEntryType.Directory, entry.DataStream);
            }
        }
    }

    /// <summary>计算文件的 SHA-256。</summary>
    private static string HashFile(string path)
    {
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexStringLower(SHA256.HashData(stream));
    }

    /// <summary>读取目录中的原始文件校验值。</summary>
    private static JsonObject ReadFileHashes(string directory)
    {
        var files = new JsonObject();
        foreach (string file in Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories).Order(StringComparer.Ordinal))
            files[Path.GetRelativePath(directory, file).Replace('\\', '/')] = HashFile(file);
        return files;
    }

    /// <summary>校验还原文件是否完整。</summary>
    private static bool CheckRestoredFiles(string directory, JsonObject state)
    {
        if (state["files"] is not JsonObject files || files.Count == 0)
            return false;
        foreach (var file in files)
        {
            string path = GetContainedPath(directory, file.Key);
            if (!File.Exists(path) || HashFile(path) != file.Value!.GetValue<string>())
                return false;
        }
        return true;
    }

    /// <summary>获取限定目录内的路径并拒绝链接目录。</summary>
    private static string GetContainedPath(string root, string relative)
    {
        string fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar);
        string path = Path.GetFullPath(Path.Combine(fullRoot, relative));
        if (relative.Contains(':') || Path.IsPathRooted(relative) || (path != fullRoot && !path.StartsWith(fullRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)))
            throw new InvalidDataException($"Unsafe package path: {relative}");
        for (string? current = path; current != null && current.Length >= fullRoot.Length; current = Path.GetDirectoryName(current))
        {
            if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException($"Unsafe package link: {current}");
        }
        return path;
    }

    /// <summary>删除包管理器工作目录。</summary>
    private static void RemoveDirectory(string directory, string root)
    {
        string path = GetContainedPath(root, Path.GetRelativePath(root, directory));
        if (string.Equals(path, Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Unsafe package directory: {path}");
        if (Directory.Exists(path))
            Directory.Delete(path, recursive: true);
    }

    /// <summary>复制变化的发布文件。</summary>
    private static void CopyFile(string source, string target)
    {
        if (File.Exists(target) && new FileInfo(source).Length == new FileInfo(target).Length && HashFile(source) == HashFile(target))
            return;
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.Copy(source, target, overwrite: true);
    }

    /// <summary>写入依赖还原状态。</summary>
    private static void WriteState(string path, JsonObject state)
    {
        File.WriteAllText(path, state.ToJsonString(JsonOptions) + "\n");
    }

    /// <summary>执行上游工具并返回退出码。</summary>
    private static async Task<int> RunProcess(string executable, string directory, IEnumerable<string> arguments)
    {
        var start = new ProcessStartInfo(executable)
        {
            WorkingDirectory = directory, UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true
        };
        foreach (string argument in arguments)
            start.ArgumentList.Add(argument);
        using Process process = Process.Start(start) ?? throw new InvalidOperationException($"Cannot start {executable}");
        await Task.WhenAll(process.WaitForExitAsync(), process.StandardOutput.BaseStream.CopyToAsync(Console.OpenStandardOutput()), process.StandardError.BaseStream.CopyToAsync(Console.OpenStandardError()));
        return process.ExitCode;
    }
}
