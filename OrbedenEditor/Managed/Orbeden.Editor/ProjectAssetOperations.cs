using System.Net;
using System.Security;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Microsoft.VisualBasic.FileIO;

namespace OrbedenEditor;

/// <summary>ProjectPanel 资源文件操作及引用维护。</summary>
internal static class ProjectAssetOperations
{
    /// <summary>移动或重命名资源并更新引用。</summary>
    public static bool Move(string source, string destination, out string message)
    {
        message = string.Empty;
        if (!TryValidateSource(source, out string sourcePath, out message)) return false;
        if (!TryValidateDestination(destination, out string destinationPath, out message)) return false;
        if (File.Exists(destinationPath) || Directory.Exists(destinationPath))
        {
            message = "Destination already exists.";
            return false;
        }

        bool directory = Directory.Exists(sourcePath);
        if (directory && IsSameOrChild(destinationPath, sourcePath))
        {
            message = "A folder cannot be moved into itself.";
            return false;
        }

        Directory.CreateDirectory(Path.GetDirectoryName(destinationPath)!);
        ReferenceRewritePlan plan = ReferenceRewritePlan.Create(sourcePath, destinationPath, deleting: false);
        if (plan.Errors.Count > 0)
        {
            message = string.Join(Environment.NewLine, plan.Errors.Take(5));
            return false;
        }

        try
        {
            if (directory) Directory.Move(sourcePath, destinationPath);
            else File.Move(sourcePath, destinationPath);

            if (!plan.Apply(out message))
            {
                if (directory) Directory.Move(destinationPath, sourcePath);
                else File.Move(destinationPath, sourcePath);
                return false;
            }

            EditorPanelRegistry.RemapAssetReferences(plan.OldResourceKey, plan.NewResourceKey, plan.Prefix);
            int liveCount = EditorAssetsNative.RemapLiveReferences(plan.OldResourceKey, plan.NewResourceKey, plan.Prefix);
            EditorAssetCatalog.Instance.Refresh();
            message = $"Moved asset and updated {plan.ChangedReferenceCount + liveCount} references.";
            return true;
        }
        catch (Exception ex)
        {
            plan.Rollback();
            message = "Move failed: " + ex.Message;
            return false;
        }
    }

    /// <summary>删除资源，清空软引用，并阻止破坏已知源文件依赖。</summary>
    public static bool Delete(string source, out string message)
    {
        message = string.Empty;
        if (!TryValidateSource(source, out string sourcePath, out message)) return false;

        ReferenceRewritePlan plan = ReferenceRewritePlan.Create(sourcePath, null, deleting: true);
        if (plan.Errors.Count > 0)
        {
            message = "Delete blocked by source dependencies:" + Environment.NewLine + string.Join(Environment.NewLine, plan.Errors.Take(5));
            return false;
        }

        if (!plan.Apply(out message)) return false;
        try
        {
            if (Directory.Exists(sourcePath))
            {
                FileSystem.DeleteDirectory(sourcePath, UIOption.OnlyErrorDialogs, RecycleOption.SendToRecycleBin);
            }
            else
            {
                FileSystem.DeleteFile(sourcePath, UIOption.OnlyErrorDialogs, RecycleOption.SendToRecycleBin);
            }

            EditorPanelRegistry.RemapAssetReferences(plan.OldResourceKey, string.Empty, plan.Prefix);
            int liveCount = EditorAssetsNative.RemapLiveReferences(plan.OldResourceKey, string.Empty, plan.Prefix);
            EditorAssetCatalog.Instance.Refresh();
            message = $"Moved asset to Recycle Bin and cleared {plan.ChangedReferenceCount + liveCount} references.";
            return true;
        }
        catch (Exception ex)
        {
            plan.Rollback();
            message = "Delete failed: " + ex.Message;
            return false;
        }
    }

    /// <summary>复制一个资源文件或文件夹。</summary>
    public static bool Duplicate(string source, out string duplicatePath, out string message)
    {
        duplicatePath = string.Empty;
        message = string.Empty;
        if (!TryValidateSource(source, out string sourcePath, out message)) return false;

        string parent = Path.GetDirectoryName(sourcePath)!;
        string name = Path.GetFileNameWithoutExtension(sourcePath);
        string extension = Directory.Exists(sourcePath) ? string.Empty : Path.GetExtension(sourcePath);
        for (int index = 1; index < 10000; index++)
        {
            string suffix = index == 1 ? " Copy" : $" Copy {index}";
            string candidate = Path.Combine(parent, name + suffix + extension);
            if (File.Exists(candidate) || Directory.Exists(candidate)) continue;
            duplicatePath = candidate;
            break;
        }

        if (string.IsNullOrEmpty(duplicatePath))
        {
            message = "Could not allocate a duplicate name.";
            return false;
        }

        try
        {
            if (Directory.Exists(sourcePath)) CopyDirectory(sourcePath, duplicatePath);
            else File.Copy(sourcePath, duplicatePath);
            EditorAssetCatalog.Instance.Refresh();
            message = "Duplicated: " + EditorAssetCatalog.Instance.ToResourceKey(duplicatePath);
            return true;
        }
        catch (Exception ex)
        {
            message = "Duplicate failed: " + ex.Message;
            return false;
        }
    }

    /// <summary>把外部文件导入当前资源文件夹。</summary>
    public static bool Import(string sourceFile, string destinationDirectory, out string importedPath, out string message)
    {
        importedPath = string.Empty;
        message = string.Empty;
        if (!File.Exists(sourceFile))
        {
            message = "Import source does not exist.";
            return false;
        }
        if (!TryValidateDestination(destinationDirectory, out string destinationPath, out message)) return false;

        try
        {
            Directory.CreateDirectory(destinationPath);
            importedPath = Path.Combine(destinationPath, Path.GetFileName(sourceFile));
            if (File.Exists(importedPath))
            {
                message = "An asset with the same name already exists.";
                return false;
            }

            File.Copy(sourceFile, importedPath);
            EditorAssetCatalog.Instance.Refresh();
            message = "Imported: " + EditorAssetCatalog.Instance.ToResourceKey(importedPath);
            return true;
        }
        catch (Exception ex)
        {
            message = "Import failed: " + ex.Message;
            return false;
        }
    }

    /// <summary>创建资源文件夹。</summary>
    public static bool CreateFolder(string path, out string message)
    {
        message = string.Empty;
        if (!TryValidateDestination(path, out string folderPath, out message)) return false;
        if (File.Exists(folderPath) || Directory.Exists(folderPath))
        {
            message = "A file or folder with the same name already exists.";
            return false;
        }

        try
        {
            Directory.CreateDirectory(folderPath);
            EditorAssetCatalog.Instance.Refresh();
            message = "Created: " + EditorAssetCatalog.Instance.ToResourceKey(folderPath);
            return true;
        }
        catch (Exception ex)
        {
            message = "Create folder failed: " + ex.Message;
            return false;
        }
    }

    //验证资源操作源路径。
    private static bool TryValidateSource(string source, out string fullPath, out string message)
    {
        fullPath = Path.GetFullPath(source);
        message = string.Empty;
        if (!EditorAssetsNative.CanModifyAssets())
        {
            message = "Assets cannot be modified while playing or without an open project.";
            return false;
        }
        if (!IsSameOrChild(fullPath, EditorAssetCatalog.Instance.ResourceRootPath)
            || string.Equals(fullPath, EditorAssetCatalog.Instance.ResourceRootPath, StringComparison.OrdinalIgnoreCase))
        {
            message = "Asset operation must stay inside the configured resource root.";
            return false;
        }
        if (!File.Exists(fullPath) && !Directory.Exists(fullPath))
        {
            message = "Asset no longer exists.";
            return false;
        }
        return true;
    }

    //验证资源操作目标路径。
    private static bool TryValidateDestination(string destination, out string fullPath, out string message)
    {
        fullPath = Path.GetFullPath(destination);
        message = string.Empty;
        if (!EditorAssetsNative.CanModifyAssets())
        {
            message = "Assets cannot be modified while playing or without an open project.";
            return false;
        }
        if (!IsSameOrChild(fullPath, EditorAssetCatalog.Instance.ResourceRootPath))
        {
            message = "Destination must stay inside the configured resource root.";
            return false;
        }
        return true;
    }

    //递归复制目录。
    private static void CopyDirectory(string source, string destination)
    {
        Directory.CreateDirectory(destination);
        foreach (string file in Directory.EnumerateFiles(source))
        {
            File.Copy(file, Path.Combine(destination, Path.GetFileName(file)));
        }
        foreach (string directory in Directory.EnumerateDirectories(source))
        {
            CopyDirectory(directory, Path.Combine(destination, Path.GetFileName(directory)));
        }
    }

    internal static bool IsSameOrChild(string path, string root)
    {
        string relative = Path.GetRelativePath(Path.GetFullPath(root), Path.GetFullPath(path));
        return relative == "." || (!relative.Equals("..", StringComparison.Ordinal)
            && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
            && !Path.IsPathRooted(relative));
    }
}

/// <summary>一次资源路径变化涉及的项目文本引用修改。</summary>
internal sealed class ReferenceRewritePlan
{
    private sealed record Rewrite(string Path, string Original, string Updated);
    private sealed record BinaryRewrite(string Path, byte[] Original, byte[] Updated);

    private static readonly Regex FieldTagRegex = new(@"<Field\b[^>]*>", RegexOptions.Compiled | RegexOptions.CultureInvariant);
    private static readonly Regex ValueAttributeRegex = new(@"(?<prefix>\bvalue\s*=\s*"")(?<value>[^""]*)(?<suffix>"")", RegexOptions.Compiled | RegexOptions.CultureInvariant);
    private static readonly Regex IncludeRegex = new(@"^(?<prefix>\s*#include\s+"")(?<path>[^""]+)(?<suffix>"".*)$", RegexOptions.Compiled | RegexOptions.Multiline | RegexOptions.CultureInvariant);
    private static readonly Regex MtlDependencyRegex = new(@"^(?<prefix>\s*(?:map_Kd|map_Bump|bump)\s+)(?<path>\S+)(?<suffix>.*)$", RegexOptions.Compiled | RegexOptions.Multiline | RegexOptions.CultureInvariant);
    private static readonly Regex MtlShaderRegex = new(@"^(?<prefix>\s*shader\s+)(?<path>\S+)(?<suffix>.*)$", RegexOptions.Compiled | RegexOptions.Multiline | RegexOptions.CultureInvariant);
    private static readonly Regex ObjMtlRegex = new(@"^(?<prefix>\s*mtllib\s+)(?<paths>[^#\r\n]+)(?<suffix>.*)$", RegexOptions.Compiled | RegexOptions.Multiline | RegexOptions.CultureInvariant);

    private readonly List<Rewrite> rewrites = [];
    private readonly List<Rewrite> applied = [];
    private readonly List<BinaryRewrite> binaryRewrites = [];
    private readonly List<BinaryRewrite> binaryApplied = [];
    private readonly string projectRoot;
    private readonly string resourceRootKey;
    private readonly string sourcePath;
    private readonly string? destinationPath;
    private readonly bool deleting;

    public string OldResourceKey { get; }
    public string NewResourceKey { get; }
    public bool Prefix { get; }
    public List<string> Errors { get; } = [];
    public int ChangedReferenceCount { get; private set; }

    private ReferenceRewritePlan(string source, string? destination, bool delete)
        : this(EditorAssetCatalog.Instance.ProjectRoot,
            EditorAssetCatalog.Instance.ResourceRootKey,
            source,
            destination,
            delete)
    {
    }

    private ReferenceRewritePlan(string ownerProjectRoot,
        string ownerResourceRootKey,
        string source,
        string? destination,
        bool delete)
    {
        projectRoot = Path.GetFullPath(ownerProjectRoot);
        resourceRootKey = NormalizeKey(ownerResourceRootKey);
        sourcePath = Path.GetFullPath(source);
        destinationPath = destination == null ? null : Path.GetFullPath(destination);
        deleting = delete;
        OldResourceKey = NormalizeKey(Path.GetRelativePath(projectRoot, sourcePath));
        NewResourceKey = destinationPath == null ? string.Empty : NormalizeKey(Path.GetRelativePath(projectRoot, destinationPath));
        Prefix = Directory.Exists(sourcePath);
    }

    /// <summary>预扫描一次资源路径变化。</summary>
    public static ReferenceRewritePlan Create(string source, string? destination, bool deleting)
    {
        ReferenceRewritePlan plan = new(source, destination, deleting);
        plan.Scan();
        return plan;
    }

    //创建资源引用计划
    internal static ReferenceRewritePlan CreateForProject(string projectRoot,
        string resourceRootKey,
        string source,
        string? destination,
        bool deleting)
    {
        ReferenceRewritePlan plan = new(projectRoot, resourceRootKey, source, destination, deleting);
        plan.Scan();
        return plan;
    }

    /// <summary>应用全部预计算文本修改。</summary>
    public bool Apply(out string message)
    {
        message = string.Empty;
        applied.Clear();
        binaryApplied.Clear();
        try
        {
            foreach (Rewrite rewrite in rewrites)
            {
                string? directory = Path.GetDirectoryName(rewrite.Path);
                if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
                File.WriteAllText(rewrite.Path, rewrite.Updated);
                applied.Add(rewrite);
            }
            foreach (BinaryRewrite rewrite in binaryRewrites)
            {
                string? directory = Path.GetDirectoryName(rewrite.Path);
                if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
                File.WriteAllBytes(rewrite.Path, rewrite.Updated);
                binaryApplied.Add(rewrite);
            }
            return true;
        }
        catch (Exception ex)
        {
            Rollback();
            message = "Reference update failed: " + ex.Message;
            return false;
        }
    }

    /// <summary>恢复已经写入的引用文件。</summary>
    public void Rollback()
    {
        foreach (Rewrite rewrite in applied.AsEnumerable().Reverse())
        {
            try
            {
                if (File.Exists(rewrite.Path)) File.WriteAllText(rewrite.Path, rewrite.Original);
            }
            catch
            {
                //忽略回滚错误
            }
        }
        foreach (BinaryRewrite rewrite in binaryApplied.AsEnumerable().Reverse())
        {
            try
            {
                if (File.Exists(rewrite.Path)) File.WriteAllBytes(rewrite.Path, rewrite.Original);
            }
            catch
            {
                //忽略回滚错误
            }
        }
        applied.Clear();
        binaryApplied.Clear();
    }

    //扫描项目中会受路径变化影响的引用文件。
    private void Scan()
    {
        if (!Directory.Exists(projectRoot)) return;
        foreach (string file in Directory.EnumerateFiles(projectRoot, "*", System.IO.SearchOption.AllDirectories))
        {
            if (!ShouldInspect(file) || (deleting && ProjectAssetOperations.IsSameOrChild(file, sourcePath))) continue;

            bool ownerMoves = !deleting && destinationPath != null && ProjectAssetOperations.IsSameOrChild(file, sourcePath);
            string ownerRelativePath = ownerMoves ? Path.GetRelativePath(sourcePath, file) : string.Empty;
            string targetFile = !ownerMoves
                ? file
                : ownerRelativePath == "." ? destinationPath! : Path.Combine(destinationPath!, ownerRelativePath);
            string oldOwnerKey = NormalizeKey(Path.GetRelativePath(projectRoot, file));
            string newOwnerKey = NormalizeKey(Path.GetRelativePath(projectRoot, targetFile));
            if (file.EndsWith(".glb", StringComparison.OrdinalIgnoreCase))
            {
                ScanGlb(file, targetFile, oldOwnerKey, newOwnerKey);
                continue;
            }

            string original;
            try
            {
                original = File.ReadAllText(file);
            }
            catch
            {
                continue;
            }

            string updated = RewriteFile(file, original, oldOwnerKey, newOwnerKey);
            if (updated == original) continue;

            rewrites.Add(new Rewrite(targetFile, original, updated));
        }
    }

    //扫描并重写 GLB JSON Chunk 中的外部 URI。
    private void ScanGlb(string file, string targetFile, string oldOwnerKey, string newOwnerKey)
    {
        try
        {
            byte[] original = File.ReadAllBytes(file);
            byte[] updated = RewriteGlb(original, oldOwnerKey, newOwnerKey);
            if (!updated.AsSpan().SequenceEqual(original)) binaryRewrites.Add(new BinaryRewrite(targetFile, original, updated));
        }
        catch (Exception ex)
        {
            if (!string.Equals(oldOwnerKey, newOwnerKey, StringComparison.Ordinal))
            {
                Errors.Add($"{oldOwnerKey}: GLB dependency scan failed: {ex.Message}");
            }
        }
    }

    //按文件类型更新引用。
    private string RewriteFile(string path, string content, string oldOwnerKey, string newOwnerKey)
    {
        string lowerPath = path.ToLowerInvariant();
        if (lowerPath.EndsWith(".world")) return RewriteWorld(content);
        if (lowerPath.EndsWith(".scripts.json")) return RewriteSidecar(content);
        if (lowerPath.EndsWith(".orbshader")) return RewriteRegexDependency(content, IncludeRegex, oldOwnerKey, newOwnerKey, relative: true);
        if (lowerPath.EndsWith(".mtl"))
        {
            string value = RewriteRegexDependency(content, MtlDependencyRegex, oldOwnerKey, newOwnerKey, relative: true);
            return RewriteRegexDependency(value, MtlShaderRegex, oldOwnerKey, newOwnerKey, relative: false);
        }
        if (lowerPath.EndsWith(".obj")) return RewriteObjDependencies(content, oldOwnerKey, newOwnerKey);
        if (lowerPath.EndsWith(".gltf")) return RewriteGltf(content, oldOwnerKey, newOwnerKey);
        return content;
    }

    //更新 world 中的 Ref<> value 属性。
    private string RewriteWorld(string content)
    {
        return FieldTagRegex.Replace(content, match =>
        {
            string tag = match.Value;
            if (!tag.Contains("type=\"Ref&lt;", StringComparison.Ordinal)) return tag;

            return ValueAttributeRegex.Replace(tag, valueMatch =>
            {
                string value = WebUtility.HtmlDecode(valueMatch.Groups["value"].Value);
                if (!TryMapSoftReference(value, out string mapped)) return valueMatch.Value;
                ChangedReferenceCount++;
                return valueMatch.Groups["prefix"].Value + (SecurityElement.Escape(mapped) ?? string.Empty) + valueMatch.Groups["suffix"].Value;
            }, 1);
        });
    }

    //更新 C# sidecar 中资源对象类型字段。
    private string RewriteSidecar(string content)
    {
        try
        {
            JsonNode? root = JsonNode.Parse(content);
            JsonArray? scripts = root?["scripts"] as JsonArray;
            if (scripts == null) return content;

            bool changed = false;
            foreach (JsonNode? script in scripts)
            {
                JsonObject? values = script?["values"] as JsonObject;
                if (values == null) continue;
                foreach ((_, JsonNode? node) in values)
                {
                    if (node is not JsonObject serialized) continue;
                    string type = serialized["type"]?.GetValue<string>() ?? string.Empty;
                    string value = serialized["value"]?.GetValue<string>() ?? string.Empty;
                    if (!IsManagedResourceType(type) || !TryMapSoftReference(value, out string mapped)) continue;
                    serialized["value"] = mapped;
                    ChangedReferenceCount++;
                    changed = true;
                }
            }

            return changed ? root!.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) : content;
        }
        catch
        {
            return content;
        }
    }

    //更新一类单路径文本依赖。
    private string RewriteRegexDependency(string content, Regex regex, string oldOwnerKey, string newOwnerKey, bool relative)
    {
        return regex.Replace(content, match =>
        {
            string reference = match.Groups["path"].Value;
            if (!TryRewriteDependency(reference, oldOwnerKey, newOwnerKey, relative, out string mapped)) return match.Value;
            return match.Groups["prefix"].Value + mapped + match.Groups["suffix"].Value;
        });
    }

    //更新 OBJ mtllib 的多路径列表。
    private string RewriteObjDependencies(string content, string oldOwnerKey, string newOwnerKey)
    {
        return ObjMtlRegex.Replace(content, match =>
        {
            string[] paths = match.Groups["paths"].Value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
            bool changed = false;
            for (int index = 0; index < paths.Length; index++)
            {
                if (!TryRewriteDependency(paths[index], oldOwnerKey, newOwnerKey, relative: true, out string mapped)) continue;
                paths[index] = mapped;
                changed = true;
            }
            return changed ? match.Groups["prefix"].Value + string.Join(' ', paths) + match.Groups["suffix"].Value : match.Value;
        });
    }

    //更新 glTF 外部 buffer 和 image URI。
    private string RewriteGltf(string content, string oldOwnerKey, string newOwnerKey)
    {
        try
        {
            JsonNode? root = JsonNode.Parse(content);
            bool changed = RewriteGltfUris(root?["buffers"] as JsonArray, oldOwnerKey, newOwnerKey);
            changed |= RewriteGltfUris(root?["images"] as JsonArray, oldOwnerKey, newOwnerKey);
            return changed ? root!.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) : content;
        }
        catch (Exception ex)
        {
            if (!string.Equals(oldOwnerKey, newOwnerKey, StringComparison.Ordinal))
            {
                Errors.Add($"{oldOwnerKey}: glTF dependency scan failed: {ex.Message}");
            }
            return content;
        }
    }

    //重建 GLB，并只替换 JSON Chunk。
    private byte[] RewriteGlb(byte[] content, string oldOwnerKey, string newOwnerKey)
    {
        using BinaryReader reader = new(new MemoryStream(content), System.Text.Encoding.UTF8, leaveOpen: false);
        uint magic = reader.ReadUInt32();
        uint version = reader.ReadUInt32();
        uint totalLength = reader.ReadUInt32();
        if (magic != 0x46546C67 || totalLength > content.Length) throw new InvalidDataException("Invalid GLB header.");

        List<(uint Type, byte[] Data)> chunks = [];
        bool changed = false;
        while (reader.BaseStream.Position + 8 <= totalLength)
        {
            uint length = reader.ReadUInt32();
            uint type = reader.ReadUInt32();
            byte[] data = reader.ReadBytes(checked((int)length));
            if (data.Length != length) throw new EndOfStreamException("Incomplete GLB chunk.");
            if (type == 0x4E4F534A)
            {
                string json = System.Text.Encoding.UTF8.GetString(data).TrimEnd('\0', ' ', '\t', '\r', '\n');
                string updatedJson = RewriteGltf(json, oldOwnerKey, newOwnerKey);
                if (updatedJson != json)
                {
                    data = System.Text.Encoding.UTF8.GetBytes(updatedJson);
                    int jsonByteLength = data.Length;
                    int paddedLength = (data.Length + 3) & ~3;
                    Array.Resize(ref data, paddedLength);
                    for (int index = jsonByteLength; index < data.Length; index++) data[index] = 0x20;
                    changed = true;
                }
            }
            chunks.Add((type, data));
        }
        if (!changed) return content;

        using MemoryStream output = new();
        using (BinaryWriter writer = new(output, System.Text.Encoding.UTF8, leaveOpen: true))
        {
            writer.Write(magic);
            writer.Write(version);
            writer.Write(0u);
            foreach ((uint type, byte[] data) in chunks)
            {
                writer.Write((uint)data.Length);
                writer.Write(type);
                writer.Write(data);
            }
            writer.BaseStream.Position = 8;
            writer.Write((uint)output.Length);
        }
        return output.ToArray();
    }

    //更新一组 glTF URI。
    private bool RewriteGltfUris(JsonArray? values, string oldOwnerKey, string newOwnerKey)
    {
        if (values == null) return false;
        bool changed = false;
        foreach (JsonNode? value in values)
        {
            if (value is not JsonObject item) continue;
            string uri = item["uri"]?.GetValue<string>() ?? string.Empty;
            if (string.IsNullOrEmpty(uri) || uri.StartsWith("data:", StringComparison.OrdinalIgnoreCase)) continue;
            if (!TryRewriteDependency(uri, oldOwnerKey, newOwnerKey, relative: true, out string mapped)) continue;
            item["uri"] = mapped;
            changed = true;
        }
        return changed;
    }

    //重映射一个显式资源软引用。
    private bool TryMapSoftReference(string value, out string mapped)
    {
        mapped = value;
        int separator = value.IndexOf("//", StringComparison.Ordinal);
        string source = separator < 0 ? NormalizeKey(value) : NormalizeKey(value[..separator]);
        string subId = separator < 0 ? string.Empty : value[separator..];
        if (!TryMapSourceKey(source, out string mappedSource)) return false;

        mapped = string.IsNullOrEmpty(mappedSource) ? string.Empty : mappedSource + subId;
        return mapped != value;
    }

    //重映射源文件依赖路径
    private bool TryRewriteDependency(string reference,
        string oldOwnerKey,
        string newOwnerKey,
        bool relative,
        out string mapped)
    {
        mapped = reference;
        if (string.IsNullOrWhiteSpace(reference)
            || reference.Contains("://", StringComparison.Ordinal)
            || reference.StartsWith("data:", StringComparison.OrdinalIgnoreCase)) return false;

        string oldTargetKey;
        if (!relative || reference.Equals(resourceRootKey, StringComparison.OrdinalIgnoreCase)
            || reference.StartsWith(resourceRootKey + "/", StringComparison.OrdinalIgnoreCase))
        {
            oldTargetKey = NormalizeKey(reference);
        }
        else
        {
            string ownerDirectory = Path.GetDirectoryName(oldOwnerKey.Replace('/', Path.DirectorySeparatorChar)) ?? string.Empty;
            string targetPath = Path.GetFullPath(Path.Combine(projectRoot, ownerDirectory, reference));
            if (!ProjectAssetOperations.IsSameOrChild(targetPath, projectRoot)) return false;
            oldTargetKey = NormalizeKey(Path.GetRelativePath(projectRoot, targetPath));
        }

        bool targetChanges = TryMapSourceKey(oldTargetKey, out string newTargetKey);
        if (deleting && targetChanges)
        {
            Errors.Add($"{oldOwnerKey} -> {reference}");
            return false;
        }

        bool ownerChanges = !string.Equals(oldOwnerKey, newOwnerKey, StringComparison.Ordinal);
        if (!targetChanges && !ownerChanges) return false;
        if (!targetChanges) newTargetKey = oldTargetKey;

        if (!relative)
        {
            mapped = newTargetKey;
            ChangedReferenceCount++;
            return mapped != reference;
        }

        string newOwnerPath = Path.Combine(projectRoot, newOwnerKey.Replace('/', Path.DirectorySeparatorChar));
        string newTargetPath = Path.Combine(projectRoot, newTargetKey.Replace('/', Path.DirectorySeparatorChar));
        mapped = NormalizeKey(Path.GetRelativePath(Path.GetDirectoryName(newOwnerPath)!, newTargetPath));
        ChangedReferenceCount++;
        return mapped != reference;
    }

    //重映射资源源文件 Key。
    private bool TryMapSourceKey(string source, out string mapped)
    {
        mapped = source;
        bool matches = string.Equals(source, OldResourceKey, StringComparison.Ordinal);
        if (!matches && Prefix && source.Length > OldResourceKey.Length)
        {
            matches = source.StartsWith(OldResourceKey, StringComparison.Ordinal) && source[OldResourceKey.Length] == '/';
        }
        if (!matches) return false;

        mapped = string.IsNullOrEmpty(NewResourceKey) ? string.Empty : NewResourceKey + source[OldResourceKey.Length..];
        return true;
    }

    //判断 sidecar 类型是否是当前支持的资源对象。
    private static bool IsManagedResourceType(string type)
    {
        string value = type.Split(',')[0].Trim();
        return value is "Orbeden.Mesh" or "Orbeden.Material" or "Orbeden.Shader";
    }

    //判断文件是否包含首版支持的资源引用语法。
    private bool ShouldInspect(string path)
    {
        if (ProjectAssetOperations.IsSameOrChild(path, Path.Combine(projectRoot, "Managed"))) return false;
        string lower = path.ToLowerInvariant();
        return lower.EndsWith(".world")
            || lower.EndsWith(".scripts.json")
            || lower.EndsWith(".orbshader")
            || lower.EndsWith(".mtl")
            || lower.EndsWith(".obj")
            || lower.EndsWith(".gltf")
            || lower.EndsWith(".glb");
    }

    private static string NormalizeKey(string path) => path.Replace('\\', '/');
}
