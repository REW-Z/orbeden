using System.Xml.Linq;
using Orbeden;

namespace OrbedenEditor;

internal enum EditorPrefabEditOperation : byte { RootTransform, Header, Structure, Move, Delete, Copy }
internal enum EditorWorldFileOperation : byte { PrepareOpen, CommitOpen, Save, Cook }
internal enum EditorWorldSnapshotOperation : byte { Capture, Restore, EnterTemporary, LeaveTemporary }

/// <summary>管理场景绑定和预制体编辑会话</summary>
internal static class EditorPrefabSession
{
    private static List<EditorPrefabBinding> bindings = [];
    private static string editingAsset = string.Empty;
    private static string editingRoot = string.Empty;
    private static List<EditorPrefabBinding> previousBindings = [];
    private static bool originalDirty;
    private static bool openedWithUpdates;
    internal static bool Updating;
    internal static bool IsEditing => editingAsset.Length != 0;

    //处理场景文件的编辑和发布边界
    internal static bool ProcessWorldFile(EditorWorldFileOperation operation, string source, string target)
    {
        string contentRoot = EditorApplication.GetProjectText(EditorProjectField.Content);
        if (operation == EditorWorldFileOperation.CommitOpen)
        {
            bindings = EditorPrefabDocuments.ReadBindings(XDocument.Load(source).Root!);
            editingAsset = editingRoot = string.Empty;
            previousBindings = [];
            if (openedWithUpdates) EditorApplication.MarkWorldDirty();
            openedWithUpdates = false;
            return true;
        }
        XDocument document = source.StartsWith("<", StringComparison.Ordinal) ? XDocument.Parse(source) : XDocument.Load(source);
        XElement root = document.Root ?? throw new InvalidDataException("Missing world document.");
        if (operation == EditorWorldFileOperation.Save)
        {
            if (IsEditing && target.Length == 0)
            {
                XElement[] roots = root.Elements("Ens").ToArray();
                if (roots.Length != 1 || (string?)roots[0].Attribute("stableId") != editingRoot)
                    throw new InvalidDataException("Keep the original prefab root and a single top-level object.");
                EditorPrefabDocuments.ReadSource(contentRoot, editingAsset);
                EditorPrefabDocuments.WriteFile(Path.Combine(contentRoot, editingAsset),
                    new XDocument(new XElement("Prefab", new XAttribute("version", "1"), new XElement(roots[0]))));
                return true;
            }
            else
            {
                HashSet<string> alive = root.Descendants("Ens").Select(e => (string)e.Attribute("stableId")!).ToHashSet(StringComparer.Ordinal);
                EditorPrefabDocuments.WriteBindings(root, bindings.Where(b => alive.Contains(b.RootKey)));
                EditorPrefabDocuments.ReadBindings(root);
                if (!root.Elements("PrefabBindings").Any()) { File.Move(source, target, true); return true; }
            }
            EditorPrefabDocuments.WriteFile(target, document);
            return true;
        }

        //直接复制无绑定场景
        if (!root.Elements("PrefabBindings").Any())
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(target))!);
            File.Copy(source, target, true);
            if (operation == EditorWorldFileOperation.PrepareOpen) openedWithUpdates = false;
            return true;
        }

        //更新带绑定的场景文档
        bool changed = false;
        if (operation == EditorWorldFileOperation.Cook)
        {
            EditorPrefabDocuments.Synchronize(document, contentRoot, null);
            document.Root!.Elements("PrefabBindings").Remove();
        }
        else
        {
            List<EditorPrefabBinding> entries = EditorPrefabDocuments.ReadBindings(root);
            foreach (string asset in entries.Select(b => b.AssetKey).Distinct(StringComparer.Ordinal))
            {
                try
                {
                    XDocument candidate = new(document);
                    changed |= EditorPrefabDocuments.Synchronize(candidate, contentRoot, asset);
                    document = candidate;
                }
                catch (Exception ex) { EditorConsole.Error("Prefab update failed: " + asset + ": " + ex.Message); }
            }
            openedWithUpdates = changed;
        }
        EditorPrefabDocuments.WriteFile(target, document);
        return true;
    }

    //查找节点所属的绑定实例
    internal static EditorPrefabBinding? FindBinding(Ens ens)
    {
        if (bindings.Count == 0) return null;
        for (Ens current = ens; current.IsValid; current = Ens.FromId(current.Transform.GetParent()))
        {
            string key = current.ResourceKey;
            EditorPrefabBinding? binding = bindings.FirstOrDefault(b => b.RootKey == key);
            if (binding != null) return binding;
        }
        return null;
    }

    //检查节点的编辑权限
    internal static bool CanModify(Ens ens, EditorPrefabEditOperation operation)
    {
        if (Updating || EditorApplication.IsPlaying) return true;
        if (!ens.IsValid) return !IsEditing;
        if (IsEditing)
            return ens.ResourceKey != editingRoot || operation is EditorPrefabEditOperation.RootTransform
                or EditorPrefabEditOperation.Header or EditorPrefabEditOperation.Structure;
        EditorPrefabBinding? binding = FindBinding(ens);
        if (binding == null) return true;
        return binding.RootKey == ens.ResourceKey && operation is EditorPrefabEditOperation.RootTransform
            or EditorPrefabEditOperation.Header or EditorPrefabEditOperation.Move or EditorPrefabEditOperation.Delete or EditorPrefabEditOperation.Copy;
    }

    //登记新实例的来源和对象映射
    internal static void RegisterInstance(string assetKey, Ens root)
    {
        XElement source = EditorPrefabDocuments.ReadSource(EditorApplication.GetProjectText(EditorProjectField.Content), assetKey);
        XElement instance = XDocument.Parse(PrepareSnapshot(EditorAssetsNative.CaptureEns(root.Id))).Root!;
        Dictionary<string, string> map = EditorPrefabDocuments.MapObjects(source, instance);
        bindings.Add(new EditorPrefabBinding { AssetKey = assetKey, RootKey = root.ResourceKey, ObjectKeys = map });
        EditorApplication.MarkWorldDirty();
    }

    //为子树快照附加绑定
    internal static string CaptureBindings(string snapshot)
    {
        if (snapshot.Length == 0 || bindings.Count == 0) return snapshot;
        XDocument document = XDocument.Parse(snapshot);
        HashSet<string> roots = document.Descendants("Ens").Select(e => (string)e.Attribute("stableId")!).ToHashSet(StringComparer.Ordinal);
        EditorPrefabDocuments.WriteBindings(document.Root!, bindings.Where(b => roots.Contains(b.RootKey)));
        return document.ToString();
    }

    //移除原生恢复入口不读取的绑定区块
    internal static string PrepareSnapshot(string snapshot)
    {
        if (!snapshot.Contains("<PrefabBindings", StringComparison.Ordinal)) return snapshot;
        XDocument document = XDocument.Parse(snapshot);
        document.Root!.Elements("PrefabBindings").Remove();
        return document.ToString();
    }

    //恢复快照中的绑定并映射复制身份
    internal static void RestoreBindings(string snapshot, Ens root, bool copy)
    {
        XDocument document = XDocument.Parse(snapshot);
        List<EditorPrefabBinding> restored = EditorPrefabDocuments.ReadBindings(document.Root!);
        if (restored.Count == 0) return;
        Dictionary<string, string>? map = copy ? EditorPrefabDocuments.MapObjects(document.Root!,
            XDocument.Parse(PrepareSnapshot(EditorAssetsNative.CaptureEns(root.Id))).Root!) : null;
        foreach (EditorPrefabBinding binding in restored)
        {
            if (map != null)
            {
                binding.RootKey = map[binding.RootKey];
                binding.ObjectKeys = binding.ObjectKeys.ToDictionary(p => p.Key, p => map[p.Value], StringComparer.Ordinal);
            }
            bindings.RemoveAll(b => b.RootKey == binding.RootKey);
            bindings.Add(binding);
        }
    }

    //移除已删除子树的绑定
    internal static void RemoveBindings(string snapshot)
    {
        if (bindings.Count == 0) return;
        HashSet<string> removed = XDocument.Parse(snapshot).Descendants("Ens")
            .Select(e => (string)e.Attribute("stableId")!).ToHashSet(StringComparer.Ordinal);
        bindings.RemoveAll(b => removed.Contains(b.RootKey));
    }

    //解除整棵实例的关联并记录撤销
    internal static bool Unpack(Ens ens)
    {
        EditorPrefabBinding? binding = FindBinding(ens);
        if (binding == null || IsEditing || EditorApplication.IsPlaying) return false;
        bindings.Remove(binding);
        EditorPropertyHistory.PushAction("Unpack Prefab",
            () =>
            {
                bindings.RemoveAll(b => b.RootKey == binding.RootKey);
                if (Ens.Find(binding.RootKey).IsValid) bindings.Add(binding);
                EditorApplication.MarkWorldDirty();
            },
            () => { bindings.RemoveAll(b => b.RootKey == binding.RootKey); EditorApplication.MarkWorldDirty(); });
        EditorApplication.MarkWorldDirty();
        EditorApplication.RequestRepaint();
        return true;
    }

    //打开独立预制体编辑场景
    internal static bool BeginEdit(string assetKey)
    {
        if (EditorApplication.IsPlaying || IsEditing) return false;
        try
        {
            XElement source = EditorPrefabDocuments.ReadSource(EditorApplication.GetProjectText(EditorProjectField.Content), assetKey);
            string xml = new XDocument(new XElement("World", new XElement(source.Element("Ens")!))).ToString();
            bool wasDirty = EditorApplication.WorldDirty;
            if (!EditorAssetsNative.ChangeWorldSnapshot(EditorWorldSnapshotOperation.EnterTemporary, xml)) return false;
            previousBindings = bindings;
            bindings = [];
            originalDirty = wasDirty;
            editingAsset = assetKey;
            editingRoot = (string)source.Element("Ens")!.Attribute("stableId")!;

            EditorPropertyHistory.Clear();
            EditorObjectField.Clear();
            EditorNativeComponents.SelectEns(Ens.Find(editingRoot).Id);
            EditorApplication.RequestRepaint();
            return true;
        }
        catch (Exception ex) { EditorConsole.Error("Cannot edit prefab: " + ex.Message); return false; }
    }

    //按选项保存预制体并返回原场景
    internal static bool EndEdit(bool save = true)
    {
        if (!IsEditing) return true;
        if (save && !EditorAssetsNative.SaveWorld()) return false;
        if (!EditorAssetsNative.ChangeWorldSnapshot(EditorWorldSnapshotOperation.LeaveTemporary, string.Empty)) return false;
        string savedAsset = editingAsset;
        editingAsset = editingRoot = string.Empty;
        bindings = previousBindings;
        previousBindings = [];
        if (originalDirty) EditorApplication.MarkWorldDirty();
        RefreshInstances(savedAsset, false);
        EditorPropertyHistory.Clear();
        EditorObjectField.Clear();
        EditorApplication.RequestRepaint();
        return true;
    }

    //更新当前场景和指定来源的全部引用场景
    internal static bool RefreshInstances(string assetKey, bool allScenes)
    {
        if (EditorApplication.IsPlaying) return false;
        string contentRoot = EditorApplication.GetProjectText(EditorProjectField.Content);
        bool success = true;
        if (IsEditing && !EditorAssetsNative.SaveWorld()) return false;
        if (!IsEditing)
        {
            try
            {
                XDocument document = XDocument.Parse(EditorAssetsNative.CaptureWorldSnapshot());
                HashSet<string> alive = document.Descendants("Ens").Select(e => (string)e.Attribute("stableId")!).ToHashSet(StringComparer.Ordinal);
                EditorPrefabDocuments.WriteBindings(document.Root!, bindings.Where(b => alive.Contains(b.RootKey)));
                if (EditorPrefabDocuments.Synchronize(document, contentRoot, assetKey))
                {
                    if (!EditorAssetsNative.ChangeWorldSnapshot(EditorWorldSnapshotOperation.Restore, document.ToString())) return false;
                    bindings = EditorPrefabDocuments.ReadBindings(document.Root!);
                    EditorPropertyHistory.Clear();
                    EditorObjectField.Clear();
                    EditorApplication.MarkWorldDirty();
                }
            }
            catch (Exception ex) { EditorConsole.Error("Current prefab update failed: " + ex.Message); success = false; }
        }
        if (allScenes)
        {
            string current = Path.GetFullPath(Path.Combine(contentRoot, EditorAssetsNative.GetWorldKey(false)));
            int count = 0;
            foreach (string path in Directory.EnumerateFiles(contentRoot, "*.world", new EnumerationOptions
                { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint }))
            {
                if (PathDefines.IsRawContentPath(Path.GetRelativePath(contentRoot, path))
                    || EditorAssetCatalog.Instance.IsGeneratedPath(path)) continue;
                if (!IsEditing && Path.GetFullPath(path).Equals(current, StringComparison.OrdinalIgnoreCase)) continue;
                try
                {
                    XDocument document = XDocument.Load(path);
                    if (!EditorPrefabDocuments.ReadBindings(document.Root!).Any(b => b.AssetKey == assetKey)) continue;
                    if (EditorPrefabDocuments.Synchronize(document, contentRoot, assetKey))
                    {
                        EditorPrefabDocuments.WriteFile(path, document);
                        ++count;
                    }
                }
                catch (Exception ex) { EditorConsole.Error("Prefab update failed: " + path + ": " + ex.Message); success = false; }
            }
            EditorConsole.Info("Updated " + count + " world file(s) for " + assetKey + ".");
        }
        EditorApplication.RequestRepaint();
        return success;
    }

    //绘制实例来源和关联操作
    internal static void DrawControls(Ens ens)
    {
        EditorPrefabBinding? binding = FindBinding(ens);
        if (binding == null || EditorApplication.IsPlaying) return;
        EditorGUI.Label("Prefab: " + binding.AssetKey);
        if (!File.Exists(Path.Combine(EditorApplication.GetProjectText(EditorProjectField.Content), binding.AssetKey)))
            EditorGUI.Warning("Prefab source is missing. Unpack to edit this instance independently.");
        if (EditorGUI.Button("Edit Prefab")) EditorWorldActions.RequestOpen(binding.AssetKey);
        EditorGUI.SameLine();
        if (EditorGUI.Button("Unpack")) Unpack(ens);
        EditorGUI.Separator();
    }

    //绘制编辑模式返回栏
    internal static void DrawEditToolbar()
    {
        if (!IsEditing) return;
        EditorGUI.Label("Editing Prefab: " + editingAsset);
        float width = Math.Max(EditorGUI.CalcButtonWidth("Apply"),
            Math.Max(EditorGUI.CalcButtonWidth("Return"), EditorGUI.CalcButtonWidth("Sync")));
        bool apply = EditorGUI.Button("Apply", width);
        EditorGUI.SameLine();
        bool discard = EditorGUI.Button("Return", width);
        EditorGUI.SameLine();
        bool sync = EditorGUI.Button("Sync", width);
        EditorGUI.Separator();
        if (apply) EndEdit();
        else if (discard) EndEdit(save: false);
        else if (sync) RefreshInstances(editingAsset, true);
    }

    //同步资源移动后的关联路径
    internal static void RemapAssetKeys(string oldKey, string newKey, bool prefix)
    {
        if (newKey.Length == 0) return;
        if (editingAsset == oldKey || prefix && editingAsset.StartsWith(oldKey + "/", StringComparison.Ordinal))
            editingAsset = newKey + editingAsset[oldKey.Length..];
        foreach (EditorPrefabBinding binding in bindings.Concat(previousBindings))
            if (binding.AssetKey == oldKey || prefix && binding.AssetKey.StartsWith(oldKey + "/", StringComparison.Ordinal))
                binding.AssetKey = newKey + binding.AssetKey[oldKey.Length..];
    }

    //清理项目会话数据
    internal static void Clear()
    {
        bindings.Clear();
        editingAsset = editingRoot = string.Empty;
        previousBindings = [];
        originalDirty = false;
        openedWithUpdates = Updating = false;
    }
}
