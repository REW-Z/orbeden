using System.Globalization;
using System.Numerics;
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
        List<Ens> instances = [];
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
            if (data.ModelNodes is not { Count: > 0 } modelNodes)
                throw new InvalidOperationException("Model hierarchy is unavailable. Reimport the model.");

            //加载节点引用的网格和材质
            const string defaultMaterialKey = "Builtin/Materials/default.orbmat";
            string[] meshKeys = modelNodes.Where(node => node.MeshKey.Length != 0)
                .Select(node => node.MeshKey).Distinct(StringComparer.Ordinal).ToArray();
            string[] materialKeys = modelNodes.SelectMany(node => node.MaterialKeys)
                .Select(key => key.Length == 0 ? defaultMaterialKey : key).Distinct(StringComparer.Ordinal).ToArray();
            Dictionary<string, Mesh> meshes = new(StringComparer.Ordinal);
            Dictionary<string, Material> materials = new(StringComparer.Ordinal);
            foreach (string key in meshKeys)
                meshes[key] = EditorAssetCatalog.Instance.Load(typeof(Mesh), key) as Mesh
                    ?? throw new InvalidOperationException("Cannot load model mesh: " + key);
            foreach (string key in materialKeys)
                materials[key] = EditorAssetCatalog.Instance.Load(typeof(Material), key) as Material
                    ?? throw new InvalidOperationException("Cannot load model material: " + key);
            EditorAssetCache.RecordLoadedImport(path);

            //按父子顺序创建节点和渲染组件
            root = EditorAssetsNative.CreateEns(Path.GetFileNameWithoutExtension(sourceKey));
            if (!root.IsValid) throw new InvalidOperationException("Cannot create model root.");
            if (!EditorNativeComponents.MoveEns(root.Id, parent, before, false))
                throw new InvalidOperationException("Cannot place model in the requested hierarchy.");
            if (placeAtPosition) root.Transform.SetLocalPosition(position);
            for (int index = 0; index < modelNodes.Count; ++index)
            {
                EditorAssetInspection.ModelNode node = modelNodes[index];
                ReadNodeTransform(node.LocalMatrix, out vector3 localPosition, out quaternion localRotation, out vector3 localScale);
                Ens instance = Ens.Create(node.Name);
                if (!instance.IsValid) throw new InvalidOperationException("Cannot create model node: " + node.Name);
                instances.Add(instance);
                Ens nodeParent = node.ParentIndex < 0 ? root : instances[node.ParentIndex];
                if (!EditorNativeComponents.MoveEns(instance.Id, nodeParent.Id, EnsId.Null, false))
                    throw new InvalidOperationException("Cannot attach model node: " + node.Name);
                instance.Transform.SetLocalPosition(localPosition);
                instance.Transform.SetLocalRotation(localRotation);
                instance.Transform.SetLocalScale(localScale);
                if (node.MeshKey.Length == 0) continue;

                StaticMeshRenderer renderer = instance.AddComponent<StaticMeshRenderer>()
                    ?? throw new InvalidOperationException("Cannot create model renderer: " + node.Name);
                renderer.mesh = meshes[node.MeshKey];
                renderer.materials = node.MaterialKeys.Select(key => (Material?)materials[key.Length == 0 ? defaultMaterialKey : key]).ToArray();
            }

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
            foreach (Ens instance in instances)
                if (instance.IsValid) EditorAssetsNative.DestroyEnsTree(instance.Id);
            EditorConsole.Error("Model instantiation failed: " + exception.Message);
        }
    }

    /// <summary>将导入节点矩阵分解为本地位置、旋转和缩放。</summary>
    private static void ReadNodeTransform(string localMatrix, out vector3 position, out quaternion rotation, out vector3 scale)
    {
        string[] fields = localMatrix.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (fields.Length != 16) throw new FormatException("Model node matrix must contain 16 numbers.");
        float[] values = new float[16];
        for (int index = 0; index < values.Length; ++index)
            if (!float.TryParse(fields[index], NumberStyles.Float, CultureInfo.InvariantCulture, out values[index]) || !float.IsFinite(values[index]))
                throw new FormatException("Model node matrix contains an invalid number.");
        Matrix4x4 matrix = new(values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7],
            values[8], values[9], values[10], values[11], values[12], values[13], values[14], values[15]);
        if (!Matrix4x4.Decompose(matrix, out Vector3 nodeScale, out Quaternion nodeRotation, out Vector3 nodePosition))
            throw new FormatException("Model node matrix cannot be represented by a Transform.");
        position = new vector3(nodePosition.X, nodePosition.Y, nodePosition.Z);
        rotation = new quaternion(nodeRotation.X, nodeRotation.Y, nodeRotation.Z, nodeRotation.W);
        scale = new vector3(nodeScale.X, nodeScale.Y, nodeScale.Z);
    }
}
