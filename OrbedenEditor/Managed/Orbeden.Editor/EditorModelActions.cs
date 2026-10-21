using System.Xml.Linq;
using Orbeden;

namespace OrbedenEditor;

/// <summary>从导入缓存创建模型层级及其撤销事务。</summary>
internal static class EditorModelActions
{
    /// <summary>在指定层级或投放位置创建模型并记录撤销。</summary>
    internal static void Instantiate(string sourceKey, EnsId parent, EnsId before, bool placeAtPosition, vector3 position)
    {
        if (EditorApplication.IsPlaying || !EditorAssetsNative.CanModifyAssets()
            || !EditorPrefabSession.CanModify(Ens.FromId(parent), EditorPrefabEditOperation.Structure)) return;
        Ens root = Ens.Null;
        try
        {
            string parentKey = parent.IsNull ? string.Empty : Ens.FromId(parent).ResourceKey;
            string beforeKey = before.IsNull ? string.Empty : Ens.FromId(before).ResourceKey;

            //读取已完成的模型导入清单
            string path = Path.GetFullPath(Path.Combine(EditorAssetCatalog.Instance.ContentRoot, sourceKey));
            EditorAssetInspection.Result data = EditorAssetInspection.Get(path);
            if (data.IsLoading)
            {
                EditorConsole.Warning("Model is still importing. Drop it again after import completes.");
                return;
            }
            if (data.IsStale) throw new InvalidOperationException(string.Join("\n", data.Messages));
            string prefab = EditorAssetCache.ReadModelPrefab(path, data);
            XElement document = XElement.Parse(prefab);
            if (document.Name != "Prefab" || document.Elements("Ens").Count() != 1 || document.Descendants("PrefabBindings").Any())
                throw new InvalidDataException("Expected a single-root imported prefab.");

            //加载预制体引用的共享网格和材质
            XElement[] fields = document.Descendants("Component").Where(component => (string?)component.Attribute("type") == nameof(StaticMeshRenderer))
                .Elements("Field").ToArray();
            string[] meshKeys = fields.Where(field => (string?)field.Attribute("name") == "mesh")
                .Select(field => (string?)field.Attribute("value") ?? string.Empty).Where(key => key.Length != 0).Distinct(StringComparer.Ordinal).ToArray();
            string[] materialKeys = fields.Where(field => (string?)field.Attribute("name") == "materials")
                .SelectMany(field => EditorModelImport.ReadMaterialKeys((string?)field.Attribute("value") ?? string.Empty))
                .Where(key => key.Length != 0).Distinct(StringComparer.Ordinal).ToArray();
            foreach (string key in meshKeys)
                if (EditorAssetCatalog.Instance.Load(typeof(Mesh), key) is not Mesh)
                    throw new InvalidOperationException("Cannot load model mesh: " + key);
            foreach (string key in materialKeys)
                if (EditorAssetCatalog.Instance.Load(typeof(Material), key) is not Material)
                    throw new InvalidOperationException("Cannot load model material: " + key);
            EditorAssetCache.RecordLoadedImport(path);

            //复制模型子树并重映射身份，不建立预制体绑定
            root = EditorAssetsNative.InstantiatePrefab(prefab, PrefabSource.SnapshotCopy, parent, before);
            if (!root.IsValid) throw new InvalidOperationException("Cannot instantiate model prefab.");
            if (placeAtPosition) root.Transform.localPosition = position;

            //记录整个模型的撤销和重做快照
            string snapshot = EditorAssetsNative.CaptureEns(root.Id);
            if (snapshot.Length == 0) throw new InvalidOperationException("Cannot capture model instance.");
            string rootKey = root.ResourceKey;
            EditorPropertyHistory.PushAction("Instantiate Model",
                () =>
                {
                    Ens instance = Ens.Find(rootKey);
                    if (!instance.IsValid || !EditorAssetsNative.DestroyEnsTree(instance.Id))
                        throw new InvalidOperationException("Cannot remove model instance.");
                },
                () =>
                {
                    Ens restoredParent = parentKey.Length == 0 ? Ens.Null : Ens.Find(parentKey);
                    Ens restoredBefore = beforeKey.Length == 0 ? Ens.Null : Ens.Find(beforeKey);
                    if ((parentKey.Length != 0 && !restoredParent.IsValid)
                        || (beforeKey.Length != 0 && !restoredBefore.IsValid))
                        throw new InvalidOperationException("Model hierarchy target no longer exists.");
                    foreach (string key in meshKeys)
                        if (EditorAssetCatalog.Instance.Load(typeof(Mesh), key) is not Mesh)
                            throw new InvalidOperationException("Cannot reload model mesh: " + key);
                    foreach (string key in materialKeys)
                        if (EditorAssetCatalog.Instance.Load(typeof(Material), key) is not Material)
                            throw new InvalidOperationException("Cannot reload model material: " + key);
                    if (!EditorAssetsNative.InstantiatePrefab(snapshot, PrefabSource.SnapshotIdentity,
                        restoredParent.Id, restoredBefore.Id).IsValid)
                        throw new InvalidOperationException("Cannot restore model instance.");
                });
            EditorNativeComponents.SelectEns(root.Id);
            EditorApplication.RequestRepaint();
        }
        catch (Exception exception)
        {
            if (root.IsValid) EditorAssetsNative.DestroyEnsTree(root.Id);
            EditorConsole.Error("Model instantiation failed: " + exception.Message);
        }
    }
}
