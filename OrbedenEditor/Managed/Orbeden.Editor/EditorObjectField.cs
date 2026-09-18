using Orbeden;
using System.Globalization;

namespace OrbedenEditor;

/// <summary>按声明类型绘制 Object 与 EnsId 引用。</summary>
internal static class EditorObjectField
{
    private sealed record Choice(string Key, string Label, int ObjectId, EnsId Owner);
    private static List<Choice> choices = [];
    private static string search = string.Empty;

    //查找声明的托管包装类型
    private static Type? FindType(string name)
    {
        Type? registered = NativeBindingRuntime.GetManagedType(name);
        if (registered != null) return registered;
        foreach (System.Reflection.Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            Type? type = assembly.GetType(name) ?? assembly.GetType("Orbeden." + name);
            if (type != null) return type;
        }
        return null;
    }

    //枚举场景对象和资源并按声明类型过滤
    private static List<Choice> CollectChoices(string declaredType, bool ensHandle, bool allowScene)
    {
        List<Choice> result = [];
        Type? expected = FindType(ensHandle ? "Orbeden.Ens" : declaredType);
        string nativeType = expected == null ? declaredType : NativeBindingRuntime.GetNativeTypeName(expected) ?? declaredType;
        string[] values = EditorNativeComponents.GetReferenceObjects(ensHandle ? "Ens" : nativeType).Split('\0');
        for (int index = 0; index + 5 < values.Length; index += 6)
        {
            if (!int.TryParse(values[index], NumberStyles.Integer, CultureInfo.InvariantCulture, out int id)) continue;
            if (!allowScene && values[index + 1].StartsWith("world://", StringComparison.Ordinal)) continue;
            Type? actual = FindType(values[index + 3]);
            if (expected != null && actual != null && !expected.IsAssignableFrom(actual)) continue;
            if (expected != null && typeof(Script).IsAssignableFrom(expected)
                && (actual == null || !expected.IsAssignableFrom(actual))) continue;
            result.Add(new Choice(values[index + 1], values[index + 2], id,
                new EnsId(uint.Parse(values[index + 4], CultureInfo.InvariantCulture), uint.Parse(values[index + 5], CultureInfo.InvariantCulture))));
        }
        if (!ensHandle && expected != null)
        {
            foreach (ObjectFieldOption option in EditorGUI.GetObjectFieldAssets(expected))
            {
                if (result.Any(choice => choice.Key == option.ResourceKey)) continue;
                result.Add(new Choice(option.ResourceKey, option.DisplayName, 0, EnsId.Null));
            }
        }
        return result.OrderBy(choice => choice.Label, StringComparer.OrdinalIgnoreCase).ToList();
    }

    //加载用户选定的引用并验证对象仍然存活
    private static bool TryLoadChoice(Choice choice, string declaredType, out Orbeden.Object? selected)
    {
        selected = choice.ObjectId == 0 ? null : NativeBindingRuntime.Wrap<Orbeden.Object>(choice.ObjectId);
        if (choice.ObjectId == 0)
        {
            Type? type = FindType(declaredType);
            if (type != null) selected = EditorGUI.LoadObjectFieldAsset(type, choice.Key);
        }
        return selected != null && selected.IsValid;
    }

    //清理场景切换和模块卸载后的选择状态
    internal static void Clear()
    {
        choices.Clear();
        search = string.Empty;
    }

    //绘制属性事务中的引用字段
    internal static bool Draw(string label, PropertyValue property, out InteropValue value)
    {
        value = property.Value;
        bool ensHandle = property.ReferenceType is "EnsId" or "Orbeden.EnsId";
        string key = string.Empty;
        int objectId = 0;
        if (property.Kind == InteropValueKind.EnsId)
        {
            property.Value.TryGet(out EnsId id);
            Ens ens = Ens.FromId(id);
            key = ens.IsValid ? ens.ResourceKey : id.IsNull ? string.Empty : "Missing Ens " + id;
        }
        else if (property.Kind == InteropValueKind.Object)
        {
            property.Value.TryGet(out objectId);
            Orbeden.Object? reference = Orbeden.Object.FindLoadedObject(objectId);
            key = reference?.ResourceKey ?? (objectId == 0 ? string.Empty : "Missing Object " + objectId);
        }
        else property.Value.TryGet(out key);
        if (!Draw(label, ensHandle ? "Ens" : property.ReferenceType, ref key, ref objectId, ensHandle, property.AllowsSceneReferences)) return false;
        value = property.Kind switch
        {
            InteropValueKind.Object => InteropValue.FromObjectId(objectId),
            InteropValueKind.EnsId => InteropValue.From(key.Length == 0 ? EnsId.Null : Ens.Find(key).Id),
            _ => InteropValue.FromStringId(key)
        };
        return true;
    }

    //绘制引用框、清空按钮和类型过滤选择器
    internal static bool Draw(string label, string declaredType, ref string key, ref int objectId, bool ensHandle = false, bool allowScene = true)
    {
        key ??= string.Empty;
        string currentKey = key;
        string name = key.Length == 0 ? "None" : EditorNativeComponents.GetReferenceLabel(key);
        if (name.Length == 0)
        {
            Type? type = FindType(declaredType);
            ObjectFieldOption? asset = type == null ? null
                : EditorGUI.GetObjectFieldAssets(type).Where(option => option.ResourceKey == currentKey).Cast<ObjectFieldOption?>().FirstOrDefault();
            name = asset?.DisplayName ?? "Missing: " + key;
        }
        string shortType = declaredType[(declaredType.LastIndexOf('.') + 1)..];
        string popup = "Select " + shortType + "##reference_picker_" + label;
        bool changed = false;
        EditorGUI.Label(label.Split("##", StringSplitOptions.None)[0]);
        EditorGUI.SameLine();
        int action = NativeEditorGUI.ReferenceField(
            EditorIconCatalog.ForReference(declaredType),
            name + " (" + shortType + ")",
            label);
        if (action == 1 && key.Length != 0)
        {
            if (key.StartsWith("world://", StringComparison.Ordinal))
            {
                Choice? target = CollectChoices(declaredType, ensHandle, allowScene).FirstOrDefault(choice => choice.Key == currentKey);
                if (target != null && !target.Owner.IsNull) EditorNativeComponents.SelectEns(target.Owner);
            }
            else ProjectPanel.Ping(key);
        }
        string draggedKey = NativeEditorGUI.ReadDrag(out int draggedKind);
        if (draggedKey.Length != 0)
        {
            List<Choice> candidates = CollectChoices(declaredType, ensHandle, allowScene);
            if (draggedKind == 1)
            {
                Choice? exact = candidates.FirstOrDefault(choice => choice.Key == draggedKey);
                EnsId owner = Ens.Find(draggedKey).Id;
                candidates = exact != null ? [exact] : candidates.Where(choice => choice.Owner.Equals(owner) && !owner.IsNull).ToList();
            }
            else
                candidates = candidates.Where(choice => choice.Key == draggedKey
                    || choice.Key.StartsWith(draggedKey + "//", StringComparison.Ordinal)).ToList();
            if (NativeEditorGUI.AcceptDrag(candidates.Count != 0))
            {
                if (candidates.Count == 1 && TryLoadChoice(candidates[0], declaredType, out Orbeden.Object? selected))
                {
                    key = candidates[0].Key;
                    objectId = selected!.InstanceId;
                    changed = true;
                }
                else if (candidates.Count > 1)
                {
                    choices = candidates;
                    search = string.Empty;
                    NativeEditorGUI.OpenPopup(popup);
                }
            }
        }
        else if (action == 2 && key.Length != 0)
        {
            key = string.Empty;
            objectId = 0;
            changed = true;
        }
        else if (action == 3)
        {
            choices = CollectChoices(declaredType, ensHandle, allowScene);
            search = string.Empty;
            NativeEditorGUI.OpenPopup(popup);
        }
        if (!NativeEditorGUI.BeginPopup(popup)) return changed;
        try
        {
            EditorGUI.InputText("Search##reference_search", ref search);
            float width = 440;
            bool visible = NativeEditorGUI.BeginChild("##reference_choices", ref width, 300);
            try
            {
                if (visible)
                {
                    foreach (Choice choice in choices)
                    {
                        if (!choice.Label.Contains(search, StringComparison.OrdinalIgnoreCase)
                            && !choice.Key.Contains(search, StringComparison.OrdinalIgnoreCase)) continue;
                        if (!EditorGUI.Selectable(choice.Label + "##" + choice.Key, key == choice.Key)) continue;
                        if (!TryLoadChoice(choice, declaredType, out Orbeden.Object? selected)) continue;
                        key = choice.Key;
                        objectId = selected!.InstanceId;
                        changed = true;
                        NativeEditorGUI.ClosePopup();
                    }
                }
            }
            finally { NativeEditorGUI.EndChild(); }
            if (EditorGUI.Button("Cancel##reference_picker")) NativeEditorGUI.ClosePopup();
        }
        finally { EditorGUI.EndPopup(); }
        return changed;
    }
}
