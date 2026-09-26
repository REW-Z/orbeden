using System.Globalization;
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
        //切项目后环境设置草稿属于上一个世界，必须丢掉
        EditorEnvironmentSettings.Invalidate();
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

    /// <summary>
    /// 源文件的导入设置类别，决定 Inspector 开不开放 Import Settings 一节以及要画哪些字段。
    ///
    /// 这是**唯一**登记导入设置的地方：新增源类型时在这里加一项，再在
    /// <see cref="DrawImportSettingsFields"/> 里补上对应控件；反查条件也从这里来，
    /// 不要再写第二份扩展名列表。
    /// </summary>
    private enum ImportSettingsKind
    {
        /// <summary>该源类型没有可编辑的导入设置。</summary>
        None,
        /// <summary>图片：一个文件对应一个 Texture2D，设置颜色空间。</summary>
        Texture,
        /// <summary>模型：设置网格缩放倍率与源坐标系上轴。</summary>
        Mesh,
    }

    private static ImportSettingsKind GetImportSettingsKind(string path) => Path.GetExtension(path).ToLowerInvariant() switch
    {
        ".png" or ".jpg" or ".jpeg" or ".tga" or ".bmp" => ImportSettingsKind.Texture,
        ".obj" or ".gltf" or ".glb" => ImportSettingsKind.Mesh,
        _ => ImportSettingsKind.None,
    };

    /// <summary>该源文件是否有可编辑的导入设置。</summary>
    internal static bool HasImportSettings(string path) => GetImportSettingsKind(path) != ImportSettingsKind.None;

    //设置草稿：数值控件拖动期间会连续返回值，不能每帧触发一次重新导入
    private static string settingsSource = string.Empty;
    private static readonly Dictionary<string, string> settingsDraft = new(StringComparer.Ordinal);
    private static bool settingsDirty;
    private static string settingsStatus = string.Empty;

    /// <summary>选择变化时重新读取草稿；同一资源的编辑不会被打断。</summary>
    private static void EnsureSettingsDraft()
    {
        if (settingsSource == SourcePath) return;
        settingsSource = SourcePath;
        settingsDraft.Clear();
        foreach ((string name, string value) in EditorAssetCache.ReadSettings(SourcePath)) settingsDraft[name] = value;
        settingsDirty = false;
        settingsStatus = string.Empty;
    }

    private static string DraftText(string key) => settingsDraft.TryGetValue(key, out string? value) ? value : string.Empty;

    //null 表示删除该键，即回到"按文件原样 / 按语义推断"
    private static void SetDraft(string key, string? value)
    {
        if (value == null) settingsDraft.Remove(key);
        else settingsDraft[key] = value;
        settingsDirty = true;
        settingsStatus = string.Empty;
    }

    /// <summary>下拉选择。选项里 Value 为 null 的一项表示"未指定"。</summary>
    private static void DrawChoice(string key, string label, IReadOnlyList<(string? Value, string Label)> options)
    {
        string current = DraftText(key);
        string preview = options.FirstOrDefault(option => (option.Value ?? string.Empty) == current).Label ?? options[0].Label;
        if (!EditorGUI.BeginCombo(label, preview)) return;
        try
        {
            foreach ((string? value, string optionLabel) in options)
                if (EditorGUI.Selectable(optionLabel, current == (value ?? string.Empty))) SetDraft(key, value);
        }
        finally { EditorGUI.EndCombo(); }
    }

    /// <summary>数值输入。未指定时显示 fallback，改动后写入草稿。</summary>
    private static void DrawNumber(string key, string label, float fallback)
    {
        string text = DraftText(key);
        float value = text.Length != 0 && float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float parsed)
            ? parsed : fallback;
        if (!EditorGUI.InputFloat(label, ref value)) return;
        if (value <= 0.0f) return;
        SetDraft(key, value.ToString("R", CultureInfo.InvariantCulture));
    }

    /// <summary>按源类型绘制设置字段。类别来自 GetImportSettingsKind，不重复判断扩展名。</summary>
    private static void DrawImportSettingsFields()
    {
        switch (GetImportSettingsKind(SourcePath))
        {
        case ImportSettingsKind.Texture:
            DrawChoice("colorSpace", "Color Space",
            [
                (null, "Auto (by usage)"),
                ("SRGB", "sRGB (color)"),
                ("Linear", "Linear (data)"),
            ]);
            EditorGUI.Label("Auto picks sRGB for color maps. Normal and mask maps should be Linear.");
            break;

        case ImportSettingsKind.Mesh:
            DrawNumber("scale", "Scale", 1.0f);
            //不列显式的 Y：未指定与显式 Y-up 行为完全相同，列两项只会让人犹豫
            DrawChoice("upAxis", "Up Axis",
            [
                (null, "Y-up (engine native)"),
                ("Z", "Z-up -> Y-up"),
            ]);
            EditorGUI.Label("The engine is Y-up. Z-up converts Blender and CAD exports on import.");
            break;
        }
    }

    /// <summary>写回设置并重新导入，让运行时对象与磁盘产物都跟上新设置。</summary>
    private static void ApplyImportSettings()
    {
        Dictionary<string, string> settings = new(settingsDraft, StringComparer.Ordinal);
        if (!EditorAssetCache.SaveSettings(SourcePath, settings, out string error))
        {
            settingsStatus = error;
            return;
        }

        settingsDirty = false;
        settingsStatus = string.Empty;
        //缓存已失效，但进程内的对象还要靠这次重导更新；设置表按源文件 Key 取用
        EditorAssetsNative.ReimportAsset(EditorAssetCatalog.Instance.ToResourceKey(SourcePath), false,
            EditorAssetCache.EncodeSettingsTable(SourcePath, settings));
    }

    /// <summary>绘制导入设置。改动先落在草稿上，Apply 才写回伴生文件并重新导入。</summary>
    private static void DrawImportSettings()
    {
        EnsureSettingsDraft();
        EditorGUI.BeginDisabled(!EditorAssetsNative.CanModifyAssets());
        try
        {
            DrawImportSettingsFields();
            EditorGUI.Separator();
            if (EditorGUI.Button(settingsDirty ? "Apply *" : "Apply")) ApplyImportSettings();
            EditorGUI.SameLine();
            if (EditorGUI.Button("Revert")) { settingsSource = string.Empty; EnsureSettingsDraft(); }
            if (settingsStatus.Length != 0) EditorGUI.Label(settingsStatus);
        }
        finally { EditorGUI.EndDisabled(); }
    }

    /// <summary>分节小标题。沿用 ProjectSettingsPanel 的做法，用可折叠节点而不是新造控件。</summary>
    private static void DrawSection(string title, string id, Action body)
    {
        int node = NativeEditorGUI.TreeNode(title + "##" + id, false, false, true);
        if ((node & 1) == 0) return;
        try { body(); }
        finally { NativeEditorGUI.TreePop(); }
    }

    /// <summary>
    /// 绘制导入产物的通用界面：导入设置（按源类型开放）＋只读对象清单。
    ///
    /// 这是所有**非 Ens、非 .orbmat** 的可检视对象的统一入口——引擎自有格式（.orbo、.orbshader）
    /// 与外部原始资源（图片、模型）都走这里。新增一种源类型时扩展这个类里的
    /// <see cref="ImportSettingsKind"/> 与 <see cref="DrawImportSettingsFields"/>，
    /// **不要**在 InspectorPanel 里加新的绘制分支。
    ///
    /// 这里只读导入清单，不把网格和纹理加载进主进程。
    /// </summary>
    internal static void Draw()
    {
        EditorGUI.Label(Path.GetFileName(SourcePath));

        //场景既不是导入资源也没有对象清单，它只有世界级渲染设置这一块
        if (Path.GetExtension(SourcePath).Equals(".world", StringComparison.OrdinalIgnoreCase))
        {
            DrawWorld();
            return;
        }

        if (HasImportSettings(SourcePath)) DrawSection("Import Settings", "asset_import_settings", DrawImportSettings);
        DrawSection("Objects", "asset_objects", DrawObjectList);
    }

    /// <summary>
    /// 世界级设置（天空盒、环境光）挂在 World 上而不是任何 Ens 上，EnsView 够不着，
    /// 所以在这里留一个入口。控件与 RenderingPanel 共用同一份实现。
    ///
    /// 启动场景标记是项目属性，任何世界都能设；环境设置只有当前打开的世界能改——
    /// 编辑器操作的是内存里的 World 对象，改不了磁盘上别的场景文件。
    /// </summary>
    private static void DrawWorld()
    {
        string key = EditorAssetCatalog.Instance.ToResourceKey(SourcePath);
        if (key.Length == 0) return;

        bool open = key.Equals(EditorAssetsNative.GetWorldKey(false), StringComparison.OrdinalIgnoreCase);
        DrawSection("World", "asset_world_status", () => DrawWorldStartup(key));

        if (open)
        {
            DrawSection("World Environment", "asset_world_environment",
                () => EditorEnvironmentSettings.Draw("asset_inspector"));
        }
        else
        {
            EditorGUI.Label("This world is not open. Open it from the Project panel to edit its environment.");
        }
    }

    /// <summary>启动场景标记。它记在 .oeproj 上，不要求这个世界当前打开。</summary>
    private static void DrawWorldStartup(string key)
    {
        bool startup = key.Equals(EditorAssetsNative.GetWorldKey(true), StringComparison.OrdinalIgnoreCase);
        EditorGUI.Label(startup ? "Startup world: yes" : "Startup world: no");

        EditorGUI.BeginDisabled(startup || !EditorAssetsNative.CanModifyAssets());
        try
        {
            if (!EditorGUI.Button("Set as Startup World")) return;
            worldStatus = EditorAssetsNative.SetStartupWorld(key)
                ? string.Empty : EditorAssetsNative.GetProjectError();
        }
        finally { EditorGUI.EndDisabled(); }

        if (worldStatus.Length != 0) EditorGUI.Label(worldStatus);
    }

    private static string worldStatus = string.Empty;

    /// <summary>导入产出的内部对象清单，只读。</summary>
    private static void DrawObjectList()
    {
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
