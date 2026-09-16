using Orbeden;

namespace OrbedenEditor;

/// <summary>管理预制体实例化及其撤销事务。</summary>
internal static class EditorPrefabActions
{
    /// <summary>实例化层级投放的预制体并记录一次撤销事务。</summary>
    internal static void Instantiate(string assetKey, EnsId parent, EnsId before, bool placeAtPosition, vector3 position)
    {
        Ens root = Ens.Null;
        try
        {
            string parentKey = parent.IsNull ? string.Empty : Ens.FromId(parent).ResourceKey;
            string beforeKey = before.IsNull ? string.Empty : Ens.FromId(before).ResourceKey;
            root = EditorAssetsNative.InstantiatePrefab(assetKey, false, parent, before);
            if (!root.IsValid) return;
            //场景投放只调整根节点的局部位置
            if (placeAtPosition) root.Transform.SetLocalPosition(position);
            string snapshot = EditorAssetsNative.CaptureEns(root.Id);
            if (snapshot.Length == 0) throw new InvalidOperationException("Cannot capture prefab instance.");
            string rootKey = root.ResourceKey;
            EditorPropertyHistory.PushAction("Instantiate Prefab",
                () =>
                {
                    Ens instance = Ens.Find(rootKey);
                    if (!instance.IsValid || !EditorAssetsNative.DestroyEnsTree(instance.Id))
                        throw new InvalidOperationException("Cannot remove prefab instance.");
                    EditorApplication.MarkWorldDirty();
                },
                () =>
                {
                    Ens restoredParent = parentKey.Length == 0 ? Ens.Null : Ens.Find(parentKey);
                    Ens restoredBefore = beforeKey.Length == 0 ? Ens.Null : Ens.Find(beforeKey);
                    if ((parentKey.Length != 0 && !restoredParent.IsValid)
                        || (beforeKey.Length != 0 && !restoredBefore.IsValid))
                        throw new InvalidOperationException("Prefab hierarchy target no longer exists.");
                    if (!EditorAssetsNative.InstantiatePrefab(snapshot, true, restoredParent.Id, restoredBefore.Id).IsValid)
                        throw new InvalidOperationException("Cannot restore prefab instance.");
                    EditorApplication.MarkWorldDirty();
                });
            EditorApplication.MarkWorldDirty();
        }
        catch (Exception ex)
        {
            if (root.IsValid) EditorAssetsNative.DestroyEnsTree(root.Id);
            Console.Error.WriteLine($"Prefab instantiation failed: {ex}");
        }
    }

}
