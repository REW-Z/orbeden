using System.Diagnostics;
using System.Text;
using System.Text.Json;
using Orbeden;

namespace OrbedenEditor;

/// <summary>索引 ProjectPanel 和 ObjectField 使用的项目资源。</summary>
internal sealed class EditorAssetCatalog : IObjectFieldAssetProvider
{
    private readonly Dictionary<Type, List<ObjectFieldOption>> assets = [];
    private string indexedProjectRoot = string.Empty;
    private string indexedResourceRoot = string.Empty;

    public static EditorAssetCatalog Instance { get; } = new();

    public string ProjectRoot => Path.GetFullPath(PathDefines.ContentRoot);
    public string ResourceRootKey => NormalizeKey(EditorAssetsNative.GetResourceRoot());
    public string ResourceRootPath => Path.GetFullPath(Path.Combine(ProjectRoot, ResourceRootKey));

    /// <summary>读取指定对象类型的可选择资源。</summary>
    public IReadOnlyList<ObjectFieldOption> GetAssets(Type objectType)
    {
        EnsureCurrentProject();
        return assets.TryGetValue(objectType, out List<ObjectFieldOption>? values) ? values : [];
    }

    /// <summary>按资源 Key 加载一个强类型资源包装。</summary>
    public Orbeden.Object? Load(Type objectType, string resourceKey)
    {
        if (objectType == typeof(Mesh)) return Mesh.Load(resourceKey);
        if (objectType == typeof(Material)) return Material.Load(resourceKey);
        if (objectType == typeof(Shader)) return Shader.Load(resourceKey);
        return null;
    }

    /// <summary>重新扫描当前项目资源。</summary>
    public void Refresh()
    {
        assets.Clear();
        indexedProjectRoot = PathDefines.ContentRoot;
        indexedResourceRoot = EditorAssetsNative.GetResourceRoot();
        if (string.IsNullOrWhiteSpace(indexedProjectRoot)) return;

        string resourcePath = ResourceRootPath;
        if (!Directory.Exists(resourcePath)) return;

        foreach (string file in Directory.EnumerateFiles(resourcePath, "*", SearchOption.AllDirectories))
        {
            AddSourceOptions(file);
        }

        foreach (List<ObjectFieldOption> options in assets.Values)
        {
            options.Sort((left, right) => string.Compare(left.ResourceKey, right.ResourceKey, StringComparison.OrdinalIgnoreCase));
        }
    }

    /// <summary>把资源磁盘路径转换为项目资源 Key。</summary>
    public string ToResourceKey(string fullPath)
    {
        string relative = Path.GetRelativePath(ProjectRoot, Path.GetFullPath(fullPath));
        return NormalizeKey(relative);
    }

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
        if (!string.Equals(indexedProjectRoot, PathDefines.ContentRoot, StringComparison.OrdinalIgnoreCase)
            || !string.Equals(indexedResourceRoot, EditorAssetsNative.GetResourceRoot(), StringComparison.OrdinalIgnoreCase))
        {
            Refresh();
        }
    }

    //按源文件生成 ObjectField 资源选项。
    private void AddSourceOptions(string file)
    {
        string sourceKey = ToResourceKey(file);
        string extension = Path.GetExtension(file).ToLowerInvariant();
        if (extension == ".orbshader")
        {
            AddOption(typeof(Shader), sourceKey, Path.GetFileName(file));
            return;
        }

        if (extension == ".obj")
        {
            AddOption(typeof(Mesh), sourceKey + "//Mesh/Main", Path.GetFileName(file) + " / Mesh/Main");
            AddObjMaterialOptions(file, sourceKey);
            return;
        }

        if (extension is ".gltf" or ".glb") AddGltfOptions(file, sourceKey);
    }

    //读取 OBJ 使用的 MTL 子资源。
    private void AddObjMaterialOptions(string objPath, string sourceKey)
    {
        try
        {
            List<string> materialFiles = [];
            foreach (string line in File.ReadLines(objPath))
            {
                string trimmed = line.Trim();
                if (!trimmed.StartsWith("mtllib ", StringComparison.Ordinal)) continue;
                foreach (string fileName in trimmed[7..].Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
                {
                    materialFiles.Add(Path.GetFullPath(Path.Combine(Path.GetDirectoryName(objPath)!, fileName)));
                }
            }

            string defaultMtl = Path.ChangeExtension(objPath, ".mtl");
            if (materialFiles.Count == 0 && File.Exists(defaultMtl)) materialFiles.Add(defaultMtl);
            foreach (string materialFile in materialFiles.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                if (!File.Exists(materialFile)) continue;
                foreach (string line in File.ReadLines(materialFile))
                {
                    string trimmed = line.Trim();
                    if (!trimmed.StartsWith("newmtl ", StringComparison.Ordinal)) continue;

                    string name = trimmed[7..];
                    string keyName = SanitizeKeyName(name, "Material");
                    AddOption(typeof(Material), sourceKey + "//Material/" + keyName, Path.GetFileName(objPath) + " / " + name);
                }
            }
        }
        catch
        {
            //坏 MTL 不影响其它资源继续显示。
        }
    }

    //读取 glTF/GLB 的 Mesh 和 Material 子资源表。
    private void AddGltfOptions(string path, string sourceKey)
    {
        try
        {
            using JsonDocument document = JsonDocument.Parse(ReadGltfJson(path));
            JsonElement root = document.RootElement;
            JsonElement meshes = root.TryGetProperty("meshes", out JsonElement meshValues) ? meshValues : default;
            int meshCount = meshes.ValueKind == JsonValueKind.Array ? meshes.GetArrayLength() : 0;
            for (int index = 0; index < meshCount; index++)
            {
                JsonElement mesh = meshes[index];
                string fallback = "Mesh_" + index;
                string name = mesh.TryGetProperty("name", out JsonElement nameValue) ? nameValue.GetString() ?? fallback : fallback;
                string id = meshCount == 1 ? "Main" : index + "_" + SanitizeKeyName(name, fallback);
                AddOption(typeof(Mesh), sourceKey + "//Mesh/" + id, Path.GetFileName(path) + " / " + name);
            }

            JsonElement materials = root.TryGetProperty("materials", out JsonElement materialValues) ? materialValues : default;
            int materialCount = materials.ValueKind == JsonValueKind.Array ? materials.GetArrayLength() : 0;
            for (int index = 0; index < materialCount; index++)
            {
                JsonElement material = materials[index];
                string fallback = "Material_" + index;
                string name = material.TryGetProperty("name", out JsonElement nameValue) ? nameValue.GetString() ?? fallback : fallback;
                string id = index + "_" + SanitizeKeyName(name, fallback);
                AddOption(typeof(Material), sourceKey + "//Material/" + id, Path.GetFileName(path) + " / " + name);
            }

            if (HasUnassignedGltfMaterial(meshes))
            {
                AddOption(typeof(Material), sourceKey + "//Material/Default", Path.GetFileName(path) + " / Default");
            }
        }
        catch
        {
            //坏 glTF 不影响目录浏览，导入器会负责输出具体错误。
        }
    }

    //读取 glTF 文本或 GLB JSON Chunk。
    private static byte[] ReadGltfJson(string path)
    {
        if (!string.Equals(Path.GetExtension(path), ".glb", StringComparison.OrdinalIgnoreCase))
        {
            return File.ReadAllBytes(path);
        }

        using BinaryReader reader = new(File.OpenRead(path), Encoding.UTF8, leaveOpen: false);
        if (reader.ReadUInt32() != 0x46546C67) throw new InvalidDataException("Invalid GLB magic.");
        reader.ReadUInt32();
        uint totalLength = reader.ReadUInt32();
        while (reader.BaseStream.Position + 8 <= totalLength)
        {
            uint chunkLength = reader.ReadUInt32();
            uint chunkType = reader.ReadUInt32();
            byte[] data = reader.ReadBytes(checked((int)chunkLength));
            if (chunkType == 0x4E4F534A) return data;
        }

        throw new InvalidDataException("GLB JSON chunk is missing.");
    }

    //判断 glTF 是否需要默认材质。
    private static bool HasUnassignedGltfMaterial(JsonElement meshes)
    {
        if (meshes.ValueKind != JsonValueKind.Array) return false;
        foreach (JsonElement mesh in meshes.EnumerateArray())
        {
            if (!mesh.TryGetProperty("primitives", out JsonElement primitives) || primitives.ValueKind != JsonValueKind.Array) continue;
            foreach (JsonElement primitive in primitives.EnumerateArray())
            {
                if (!primitive.TryGetProperty("material", out _)) return true;
            }
        }

        return false;
    }

    //添加去重后的资源选择项。
    private void AddOption(Type type, string key, string displayName)
    {
        if (!assets.TryGetValue(type, out List<ObjectFieldOption>? values))
        {
            values = [];
            assets.Add(type, values);
        }

        if (values.Any(option => string.Equals(option.ResourceKey, key, StringComparison.Ordinal))) return;
        values.Add(new ObjectFieldOption(key, displayName));
    }

    //生成与原生导入器一致的子资源 Key 片段。
    private static string SanitizeKeyName(string text, string fallback)
    {
        StringBuilder result = new(text.Length);
        foreach (char character in text)
        {
            if (character <= 127 && (char.IsLetterOrDigit(character) || character is '_' or '-' or '.')) result.Append(character);
            else if (char.IsWhiteSpace(character) || character is '/' or '\\') result.Append('_');
        }

        return result.Length == 0 ? fallback : result.ToString();
    }

    private static string NormalizeKey(string path)
    {
        string value = path.Replace('\\', '/');
        while (value.StartsWith("./", StringComparison.Ordinal)) value = value[2..];
        return value;
    }
}
