using Orbeden;

namespace OrbedenEditor;

/// <summary>共享资源检查选择和后台生成的只读资源清单。</summary>
internal static class EditorAssetInspection
{
    internal sealed record Field(string Name, string TypeName, string Value);
    internal sealed record Asset(string Key, string TypeName, string BlobName, List<Field> Fields);
    internal sealed record Result(IReadOnlyList<Asset> Objects, IReadOnlyList<string> Messages, bool IsStale = false, bool IsLoading = false);
    private static string contentRoot = string.Empty;
    internal static string SourcePath { get; private set; } = string.Empty;
    internal static string ObjectKey { get; private set; } = string.Empty;

    /// <summary>保留资源检查目标，切到 Inspector 时不丢失。</summary>
    internal static void Select(string? path, string key = "")
    {
        SourcePath = path ?? string.Empty;
        ObjectKey = key;
        if (SourcePath.Length != 0) EditorNativeComponents.SelectEns(EnsId.Null);
        EditorApplication.RequestRepaint();
    }

    /// <summary>刷新数据库验证状态，项目切换清除资源选择。</summary>
    internal static void Invalidate(bool force = false)
    {
        EditorAssetCache.Invalidate(force);
        if (contentRoot == PathDefines.ContentRoot) return;
        contentRoot = PathDefines.ContentRoot;
        SourcePath = ObjectKey = string.Empty;
    }

    /// <summary>跟随文件或目录移动更新检查 Key，删除时清除目标。</summary>
    internal static void Remap(string oldKey, string newKey, bool prefix)
    {
        Invalidate();
        if (SourcePath.Length == 0) return;
        string source = EditorAssetCatalog.Instance.ToResourceKey(SourcePath);
        if (source != oldKey && !(prefix && source.StartsWith(oldKey + "/", StringComparison.Ordinal))) return;
        if (newKey.Length == 0) { Select(null); return; }
        SourcePath = Path.GetFullPath(Path.Combine(PathDefines.ContentRoot, newKey + source[oldKey.Length..]));
        if (ObjectKey == oldKey || ObjectKey.StartsWith(oldKey + "//", StringComparison.Ordinal)
            || (prefix && ObjectKey.StartsWith(oldKey + "/", StringComparison.Ordinal))) ObjectKey = newKey + ObjectKey[oldKey.Length..];
    }

    /// <summary>扩展名只用于选择导入器，不推断子资源。</summary>
    internal static bool CanInspect(string path) => Path.GetExtension(path).ToLowerInvariant() is
        ".obj" or ".orbmat" or ".gltf" or ".glb" or ".png" or ".jpg" or ".jpeg" or ".tga" or ".bmp"
        or ".orbshader" or ".glsl" or ".orbo";

    /// <summary>读取轻量清单；大文件导入由独立进程完成。</summary>
    internal static Result Get(string path)
    {
        if (contentRoot != PathDefines.ContentRoot) Invalidate();
        if (!File.Exists(path) || !EditorAssetCatalog.Instance.IsInsideContentRoot(path)) return new([], ["Source file is missing or inaccessible."]);
        if (!CanInspect(path)) return new([], []);
        return EditorAssetCache.Get(path);
    }

    /// <summary>该源文件是否支持导入设置。目前只有图片：它一个文件对应一个 Texture2D。</summary>
    internal static bool HasImportSettings(string path) => Path.GetExtension(path).ToLowerInvariant() is
        ".png" or ".jpg" or ".jpeg" or ".tga" or ".bmp";

    /// <summary>颜色空间的当前取值；null 表示未指定，由导入器按用途推断。</summary>
    internal static string? GetColorSpaceSetting()
    {
        Dictionary<string, string> settings = EditorAssetCache.ReadSettings(SourcePath);
        return settings.TryGetValue("colorSpace", out string? value) ? value : null;
    }

    /// <summary>写入颜色空间设置并立即重新导入，让运行时对象与新设置一致。</summary>
    internal static void ApplyColorSpaceSetting(string? value)
    {
        Dictionary<string, string> settings = EditorAssetCache.ReadSettings(SourcePath);
        if (value == null) settings.Remove("colorSpace");
        else settings["colorSpace"] = value;

        if (!EditorAssetCache.SaveSettings(SourcePath, settings, out string error))
        {
            status = error;
            return;
        }

        status = string.Empty;
        //缓存已失效，但进程内的对象还要靠这次重导更新；设置表按源文件 Key 取用
        EditorAssetsNative.ReimportAsset(EditorAssetCatalog.Instance.ToResourceKey(SourcePath), false,
            EditorAssetCache.EncodeSettingsTable(SourcePath, settings));
    }

    private static string status = string.Empty;

    /// <summary>绘制导入设置。改动会写回伴生文件并触发重新导入。</summary>
    private static void DrawImportSettings()
    {
        Dictionary<string, string> settings = EditorAssetCache.ReadSettings(SourcePath);
        settings.TryGetValue("colorSpace", out string? colorSpace);
        string preview = colorSpace switch
        {
            "SRGB" => "sRGB (color)",
            "Linear" => "Linear (data)",
            _ => "Auto (by usage)",
        };

        EditorGUI.Label("Import Settings");
        EditorGUI.BeginDisabled(!EditorAssetsNative.CanModifyAssets());
        try
        {
            if (EditorGUI.BeginCombo("Color Space", preview))
            {
                try
                {
                    if (EditorGUI.Selectable("Auto (by usage)", colorSpace == null)) ApplyColorSpaceSetting(null);
                    if (EditorGUI.Selectable("sRGB (color)", colorSpace == "SRGB")) ApplyColorSpaceSetting("SRGB");
                    if (EditorGUI.Selectable("Linear (data)", colorSpace == "Linear")) ApplyColorSpaceSetting("Linear");
                }
                finally { EditorGUI.EndCombo(); }
            }
            EditorGUI.Label("Auto picks sRGB for color maps. Changing this reimports the asset.");
            if (status.Length != 0) EditorGUI.Label(status);
        }
        finally { EditorGUI.EndDisabled(); }
    }

    /// <summary>显示导入时的反射摘要，不为浏览而加载网格和纹理对象。</summary>
    internal static void Draw()
    {
        EditorGUI.Label(Path.GetFileName(SourcePath));
        if (HasImportSettings(SourcePath))
        {
            DrawImportSettings();
            EditorGUI.Separator();
        }

        EditorGUI.Label("Imported / cooked resource (read only)");
        Result result = Get(SourcePath);
        foreach (string message in result.Messages) EditorGUI.Label(message);
        bool any = false;
        foreach (Asset asset in result.Objects.Where(asset => ObjectKey.Length == 0 || asset.Key == ObjectKey))
        {
            any = true;
            bool expanded = EditorGUI.BeginCollapsibleComponentBlock(asset.TypeName,
                EditorIconCatalog.ForReference(asset.TypeName), "asset_" + asset.Key);
            try
            {
                if (!expanded) continue;
                EditorGUI.Label(asset.Key);
                foreach (Field field in asset.Fields) EditorGUI.Label(field.Name + ": " + field.Value);
                if (asset.Fields.Count == 0) EditorGUI.Label("No reflected fields.");
            }
            finally { EditorGUI.EndComponentBlock(); }
        }
        if (!any && !result.IsLoading) EditorGUI.Label(ObjectKey.Length == 0 ? "No resource objects in this file." : "Selected sub-resource no longer exists.");
    }
}
