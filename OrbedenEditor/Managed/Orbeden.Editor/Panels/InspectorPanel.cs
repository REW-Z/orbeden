using System.Globalization;
using System.Reflection;
using System.Runtime.Loader;
using Orbeden;

namespace OrbedenEditor;

/// <summary>显示并编辑当前 Ens 上的 C++ 与 C# 组件。</summary>
internal sealed class InspectorPanel : EditorPanel
{
    private sealed class GameAssemblyLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver resolver;

        /// <summary>创建可卸载的用户游戏程序集上下文。</summary>
        public GameAssemblyLoadContext(string assemblyPath) : base(isCollectible: true)
        {
            resolver = new AssemblyDependencyResolver(assemblyPath);
        }

        /// <summary>从内存加载程序集，避免锁定构建输出文件。</summary>
        public Assembly LoadAssemblyFile(string assemblyPath)
        {
            using FileStream assemblyStream = File.OpenRead(assemblyPath);
            string symbolPath = Path.ChangeExtension(assemblyPath, ".pdb");
            if (!File.Exists(symbolPath)) return LoadFromStream(assemblyStream);

            using FileStream symbolStream = File.OpenRead(symbolPath);
            return LoadFromStream(assemblyStream, symbolStream);
        }

        /// <summary>解析用户游戏程序集依赖。</summary>
        protected override Assembly? Load(AssemblyName assemblyName)
        {
            Assembly runtimeAssembly = typeof(ScriptBehaviour).Assembly;
            if (assemblyName.Name == runtimeAssembly.GetName().Name) return runtimeAssembly;
            string? path = resolver.ResolveAssemblyToPath(assemblyName);
            return path != null ? LoadAssemblyFile(path) : null;
        }
    }

    private readonly record struct ComponentAddChoice(
        string TypeName,
        string Label,
        bool IsManaged,
        Type? ManagedType);

    private sealed class FieldSnapshot
    {
        public string Name = string.Empty;
        public NativeFieldKind Kind;
        public string TypeName = string.Empty;
        public string Value = string.Empty;
    }

    private sealed class ComponentSnapshot
    {
        public EnsId Ens;
        public string TypeName = string.Empty;
        public bool IsManaged;
        public int ObjectId;
        public List<FieldSnapshot> Fields = [];
    }

    private readonly List<Type> scriptTypes = [];
    private GameAssemblyLoadContext? gameContext;
    private Assembly? gameAssembly;
    private string componentSearch = string.Empty;
    private string status = "Game assembly is not loaded.";

    public override EditorPanelInfo Info => new(
        "inspector",
        "Inspector",
        true,
        new vector2(360.0f, 520.0f),
        PanelDockPlacement.Right,
        0.25f,
        300);

    /// <summary>加载 Inspector 用于发现 C# 脚本类型的游戏程序集。</summary>
    public override void OnGameAssemblyLoaded(string assemblyPath)
    {
        LoadGameAssembly(assemblyPath);
    }

    /// <summary>卸载 Inspector 持有的游戏程序集。</summary>
    public override void OnGameAssemblyUnloaded()
    {
        UnloadGameAssembly();
    }

    /// <summary>C# 字段由 World 的原生宿主统一管理。</summary>
    public override void OnAssetReferencesRemapped(string oldKey, string newKey, bool prefix)
    {
    }

    /// <summary>Inspector 不再保存独立脚本文件。</summary>
    public override bool SavePendingChanges() => true;

    /// <summary>绘制当前选择对象及其组件。</summary>
    public override void Draw(EditorPanelContext context)
    {
        if (context.SelectedEns.IsNull)
        {
            EditorGUI.Label("No Ens selected.");
            return;
        }

        List<EnsId> selection = GetValidSelection(context.SelectedEns, context.SelectedEnsList);
        if (selection.Count == 0)
        {
            EditorGUI.Label("Selected Ens is not alive.");
            return;
        }

        Ens active = Ens.FromId(context.SelectedEns);
        DrawObjectHeader(active, selection, context.SelectedStableId);
        if (!string.IsNullOrWhiteSpace(status)) EditorGUI.Label($"C# Assembly: {status}");
        DrawComponents(selection);
        DrawAddComponent(selection);
    }

    //加载用户程序集并缓存可添加的具体 C# 脚本类型。
    private void LoadGameAssembly(string assemblyPath)
    {
        UnloadReflectionAssembly();
        scriptTypes.Clear();
        if (string.IsNullOrWhiteSpace(assemblyPath) || !File.Exists(assemblyPath))
        {
            status = "Game assembly is not loaded.";
            return;
        }

        try
        {
            gameContext = new GameAssemblyLoadContext(assemblyPath);
            gameAssembly = gameContext.LoadAssemblyFile(Path.GetFullPath(assemblyPath));
            foreach (Type type in GetLoadableTypes(gameAssembly))
            {
                if (type.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(type)) continue;
                if (type.GetConstructor([typeof(Ens)]) == null) continue;
                scriptTypes.Add(type);
            }
            scriptTypes.Sort((left, right) =>
                string.Compare(GetScriptTypeName(left), GetScriptTypeName(right), StringComparison.Ordinal));
            status = $"Loaded: {Path.GetFileName(assemblyPath)}";
        }
        catch (Exception exception)
        {
            UnloadReflectionAssembly();
            scriptTypes.Clear();
            status = "Game assembly load failed: " + exception.Message;
        }
    }

    //卸载用户程序集并清空类型缓存。
    private void UnloadGameAssembly()
    {
        UnloadReflectionAssembly();
        scriptTypes.Clear();
        componentSearch = string.Empty;
        status = "Game assembly is not loaded.";
    }

    //卸载仅供 Inspector 反射的可收集程序集上下文。
    private void UnloadReflectionAssembly()
    {
        gameAssembly = null;
        if (gameContext == null) return;
        gameContext.Unload();
        gameContext = null;
    }

    //读取程序集中的可加载类型，忽略单个坏类型。
    private static IEnumerable<Type> GetLoadableTypes(Assembly assembly)
    {
        try
        {
            return assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException exception)
        {
            return exception.Types.Where(type => type != null)!;
        }
    }

    //建立去重且仍然存活的多选列表。
    private static List<EnsId> GetValidSelection(EnsId active, IReadOnlyList<EnsId> selected)
    {
        IEnumerable<EnsId> candidates = selected.Count == 0 ? [active] : selected;
        List<EnsId> result = [];
        foreach (EnsId id in candidates)
        {
            if (result.Contains(id)) continue;
            if (Ens.FromId(id).IsValid) result.Add(id);
        }
        if (Ens.FromId(active).IsValid)
        {
            result.Remove(active);
            result.Insert(0, active);
        }
        return result;
    }

    //绘制对象名称与运行时身份。
    private static void DrawObjectHeader(Ens active, IReadOnlyList<EnsId> selection, string stableId)
    {
        EditorGUI.BeginComponentBlock("Selected Ens");
        try
        {
            List<IPropertyTarget> targets = selection
                .Select(Ens.FromId)
                .Where(value => value.IsValid)
                .Select(value => (IPropertyTarget)new DelegatedPropertyTarget(
                    $"ens:{value.Id.id}:{value.Id.version}",
                    "Name",
                    InteropValueKind.String,
                    () => InteropValue.From(value.Name),
                    updated =>
                    {
                        if (!updated.TryGet(out string name)) return InteropStatus.TypeMismatch;
                        value.Name = name;
                        return InteropStatus.Ok;
                    },
                    EditorApplication.MarkWorldDirty))
                .ToList();
            DrawPropertyDocument(new PropertyDocument(targets), "Ens");
            EditorGUI.Label($"Runtime Id: {active.Id.id}:{active.Id.version}");
            if (selection.Count > 1) EditorGUI.Label($"Selected: {selection.Count} Ens");
            EditorGUI.Label(string.IsNullOrEmpty(stableId) ? "Stable Id: <none>" : $"Stable Id: {stableId}");
        }
        finally
        {
            EditorGUI.EndComponentBlock();
        }
    }

    //按活动对象挂载顺序绘制所有选择对象共同拥有的组件。
    private void DrawComponents(IReadOnlyList<EnsId> selection)
    {
        List<NativeComponentInfo> primary = EditorNativeComponents.GetComponents(selection[0]);
        Dictionary<(string TypeName, bool Managed), int> occurrences = [];
        foreach (NativeComponentInfo component in primary)
        {
            var key = (component.TypeName, component.IsManaged);
            int occurrence = occurrences.TryGetValue(key, out int count) ? count : 0;
            occurrences[key] = occurrence + 1;

            List<NativeComponentInfo> matches = [];
            foreach (EnsId target in selection)
            {
                NativeComponentInfo match = FindOccurrence(
                    EditorNativeComponents.GetComponents(target),
                    component.TypeName,
                    component.IsManaged,
                    occurrence);
                if (match.ObjectId == 0)
                {
                    matches.Clear();
                    break;
                }
                matches.Add(match);
            }
            if (matches.Count != selection.Count) continue;
            DrawComponent(selection, matches, occurrence);
        }
    }

    //查找某个域和类型的第 occurrence 个组件。
    private static NativeComponentInfo FindOccurrence(
        IReadOnlyList<NativeComponentInfo> components,
        string typeName,
        bool isManaged,
        int occurrence)
    {
        int current = 0;
        foreach (NativeComponentInfo component in components)
        {
            if (component.IsManaged != isManaged
                || !string.Equals(component.TypeName, typeName, StringComparison.Ordinal))
                continue;
            if (current++ == occurrence) return component;
        }
        return default;
    }

    //绘制一个多目标组件属性文档。
    private void DrawComponent(
        IReadOnlyList<EnsId> selection,
        IReadOnlyList<NativeComponentInfo> components,
        int occurrence)
    {
        NativeComponentInfo primary = components[0];
        string title = GetComponentTitle(primary);
        bool removable = primary.IsManaged
            || !string.Equals(primary.TypeName, "TransformComponent", StringComparison.Ordinal);
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock(
            title,
            $"component_{primary.IsManaged}_{primary.TypeName}_{occurrence}",
            removable,
            out bool removeRequested);
        try
        {
            if (expanded && !removeRequested)
            {
                List<IPropertyTarget> targets = components
                    .Select(component => (IPropertyTarget)new NativeComponentPropertyTarget(
                        component,
                        EditorApplication.MarkWorldDirty))
                    .ToList();
                DrawPropertyDocument(new PropertyDocument(targets), title);
            }
        }
        finally
        {
            EditorGUI.EndComponentBlock();
        }

        if (removeRequested) RemoveComponentGroup(selection, components, title);
    }

    //生成带语言域标记的组件标题。
    private string GetComponentTitle(NativeComponentInfo component)
    {
        if (!component.IsManaged) return $"[C++] {GetShortTypeName(component.TypeName)}";
        bool missing = gameAssembly != null && FindScriptType(component.TypeName) == null;
        return missing
            ? $"[C#] Missing Script ({GetShortTypeName(component.TypeName)})"
            : $"[C#] {GetShortTypeName(component.TypeName)}";
    }

    //绘制 PropertyDocument 支持的全部基础值。
    private static void DrawPropertyDocument(PropertyDocument document, string undoPrefix)
    {
        document.Update();
        foreach (PropertyValue property in document.Properties)
        {
            string label = property.HasMultipleDifferentValues
                ? $"{property.Name} (Mixed)"
                : property.Name;
            if (!TryDrawProperty(label, property, out InteropValue value)) continue;
            property.SetValue(value);
            document.ApplyChanges($"Edit {undoPrefix}.{property.Name}");
        }
    }

    //绘制单个属性并返回用户提交的新值。
    private static bool TryDrawProperty(string label, PropertyValue property, out InteropValue value)
    {
        value = property.Value;
        switch (property.Kind)
        {
            case InteropValueKind.Bool:
            {
                property.Value.TryGet(out bool current);
                if (!EditorGUI.Checkbox(label, ref current)) return false;
                value = InteropValue.From(current);
                return true;
            }
            case InteropValueKind.Int32:
            {
                property.Value.TryGet(out int current);
                if (!EditorGUI.InputInt(label, ref current)) return false;
                value = InteropValue.From(current);
                return true;
            }
            case InteropValueKind.Float32:
            {
                property.Value.TryGet(out float current);
                if (!EditorGUI.InputFloat(label, ref current)) return false;
                value = InteropValue.From(current);
                return true;
            }
            case InteropValueKind.Vector3:
            {
                property.Value.TryGet(out vector3 current);
                if (!EditorGUI.InputVector3(label, ref current)) return false;
                value = InteropValue.From(current);
                return true;
            }
            case InteropValueKind.String:
            case InteropValueKind.StringId:
            {
                property.Value.TryGet(out string current);
                current ??= string.Empty;
                if (!EditorGUI.InputText(label, ref current)) return false;
                value = property.Kind == InteropValueKind.String
                    ? InteropValue.From(current)
                    : InteropValue.FromStringId(current);
                return true;
            }
            default:
            {
                string current = EditorInteropValueText.Format(property.Value);
                if (!EditorGUI.InputText(label, ref current)) return false;
                return EditorInteropValueText.TryParse(property.Kind, current, out value);
            }
        }
    }

    //绘制统一的 C++ / C# 添加组件菜单。
    private void DrawAddComponent(IReadOnlyList<EnsId> selection)
    {
        List<ComponentAddChoice> choices = [];
        foreach (string typeName in EditorNativeComponents.GetAddableTypes())
        {
            choices.Add(new ComponentAddChoice(
                typeName,
                $"[C++] {GetShortTypeName(typeName)}",
                false,
                null));
        }
        foreach (Type type in scriptTypes)
        {
            string typeName = GetScriptTypeName(type);
            choices.Add(new ComponentAddChoice(
                typeName,
                $"[C#] {GetShortTypeName(typeName)}",
                true,
                type));
        }
        choices.Sort((left, right) => string.Compare(left.Label, right.Label, StringComparison.Ordinal));

        if (choices.Count == 0)
        {
            EditorGUI.Label("No component types are available.");
            return;
        }

        if (!EditorGUI.BeginCombo("Component##add_component", "Add Component")) return;
        try
        {
            EditorGUI.InputText("Search##add_component_search", ref componentSearch);
            foreach (ComponentAddChoice choice in choices)
            {
                if (!string.IsNullOrWhiteSpace(componentSearch)
                    && choice.Label.IndexOf(componentSearch, StringComparison.OrdinalIgnoreCase) < 0)
                    continue;
                if (!EditorGUI.Selectable($"{choice.Label}##add_{choice.IsManaged}_{choice.TypeName}")) continue;
                AddComponentGroup(selection, choice);
                componentSearch = string.Empty;
            }
        }
        finally
        {
            EditorGUI.EndCombo();
        }
    }

    //原子地为全部选择对象添加同一种组件。
    private void AddComponentGroup(IReadOnlyList<EnsId> selection, ComponentAddChoice choice)
    {
        List<ComponentSnapshot> created = [];
        foreach (EnsId ens in selection)
        {
            int objectId = EditorNativeComponents.AddComponentAndGetId(ens, choice.TypeName);
            if (objectId == 0)
            {
                RemoveSnapshots(created);
                status = $"Component add failed: {choice.TypeName}";
                return;
            }

            if (choice.IsManaged && choice.ManagedType != null)
                InitializeManagedFields(objectId, choice.ManagedType);

            NativeComponentInfo component = new(objectId, choice.TypeName, choice.IsManaged);
            created.Add(CaptureComponent(ens, component));
        }

        TouchWorld();
        string label = $"Add {choice.Label}";
        EditorPropertyHistory.PushAction(
            label,
            () =>
            {
                RemoveSnapshots(created);
                TouchWorld();
            },
            () =>
            {
                RestoreSnapshots(created);
                TouchWorld();
            });
    }

    //把 C# 可序列化字段定义和默认值写进新原生宿主。
    private static void InitializeManagedFields(int objectId, Type type)
    {
        HashSet<string> existing = [];
        int existingCount = EditorNativeComponents.GetFieldCount(objectId);
        for (int index = 0; index < existingCount; ++index)
            existing.Add(EditorNativeComponents.GetFieldName(objectId, index));

        foreach (FieldInfo field in GetSerializableFields(type))
        {
            if (existing.Contains(field.Name)) continue;
            InteropValueKind kind = EditorManagedInteropValue.GetKind(field.FieldType);
            if (kind == InteropValueKind.Empty) continue;
            EditorNativeComponents.SetManagedField(
                objectId,
                field.Name,
                GetManagedFieldTypeName(field.FieldType, kind),
                GetDefaultSerializedValue(kind),
                field.GetCustomAttribute<HideInInspectorAttribute>() == null);
        }
    }

    //原子删除一组匹配的组件，并把可恢复快照压入 Undo。
    private void RemoveComponentGroup(
        IReadOnlyList<EnsId> selection,
        IReadOnlyList<NativeComponentInfo> components,
        string title)
    {
        List<ComponentSnapshot> snapshots = [];
        for (int index = 0; index < components.Count; ++index)
            snapshots.Add(CaptureComponent(selection[index], components[index]));

        int removed = 0;
        for (; removed < snapshots.Count; ++removed)
        {
            if (EditorNativeComponents.RemoveComponent(snapshots[removed].ObjectId)) continue;
            RestoreSnapshots(snapshots.Take(removed));
            status = $"Component remove failed: {title}";
            return;
        }

        TouchWorld();
        EditorPropertyHistory.PushAction(
            $"Remove {title}",
            () =>
            {
                RestoreSnapshots(snapshots);
                TouchWorld();
            },
            () =>
            {
                RemoveSnapshots(snapshots);
                TouchWorld();
            });
    }

    //捕获一个组件所有可见持久化字段。
    private static ComponentSnapshot CaptureComponent(EnsId ens, NativeComponentInfo component)
    {
        ComponentSnapshot snapshot = new()
        {
            Ens = ens,
            TypeName = component.TypeName,
            IsManaged = component.IsManaged,
            ObjectId = component.ObjectId,
        };
        int count = EditorNativeComponents.GetFieldCount(component.ObjectId);
        for (int index = 0; index < count; ++index)
        {
            NativeFieldKind kind = EditorNativeComponents.GetFieldKind(component.ObjectId, index);
            snapshot.Fields.Add(new FieldSnapshot
            {
                Name = EditorNativeComponents.GetFieldName(component.ObjectId, index),
                Kind = kind,
                TypeName = GetNativeFieldTypeName(kind),
                Value = EditorNativeComponents.GetFieldValue(component.ObjectId, index),
            });
        }
        return snapshot;
    }

    //删除快照当前指向的组件实例。
    private static void RemoveSnapshots(IEnumerable<ComponentSnapshot> snapshots)
    {
        foreach (ComponentSnapshot snapshot in snapshots.Reverse())
        {
            if (snapshot.ObjectId != 0) EditorNativeComponents.RemoveComponent(snapshot.ObjectId);
            snapshot.ObjectId = 0;
        }
    }

    //重新创建组件并恢复字段值。
    private static void RestoreSnapshots(IEnumerable<ComponentSnapshot> values)
    {
        foreach (ComponentSnapshot snapshot in values)
        {
            int objectId = EditorNativeComponents.AddComponentAndGetId(snapshot.Ens, snapshot.TypeName);
            if (objectId == 0) continue;
            snapshot.ObjectId = objectId;

            if (snapshot.IsManaged)
            {
                foreach (FieldSnapshot field in snapshot.Fields)
                {
                    if (field.Name == "enabled" || field.Kind == NativeFieldKind.Unsupported) continue;
                    EditorNativeComponents.SetManagedField(
                        objectId,
                        field.Name,
                        field.TypeName,
                        field.Value,
                        true);
                }
            }

            int fieldCount = EditorNativeComponents.GetFieldCount(objectId);
            foreach (FieldSnapshot field in snapshot.Fields)
            {
                for (int index = 0; index < fieldCount; ++index)
                {
                    if (!string.Equals(
                        EditorNativeComponents.GetFieldName(objectId, index),
                        field.Name,
                        StringComparison.Ordinal))
                        continue;
                    EditorNativeComponents.SetFieldValue(objectId, index, field.Value);
                    break;
                }
            }
        }
    }

    //标记 World 并刷新 Inspector；PIE 中 MarkWorldDirty 会自动忽略磁盘 Dirty。
    private static void TouchWorld()
    {
        EditorApplication.MarkWorldDirty();
        EditorApplication.RequestRepaint();
    }

    //按完整名称查找 Inspector 已加载的 C# 类型。
    private Type? FindScriptType(string typeName)
    {
        return scriptTypes.FirstOrDefault(type =>
            string.Equals(GetScriptTypeName(type), typeName, StringComparison.Ordinal));
    }

    //枚举从基类到派生类声明的可序列化字段。
    private static IEnumerable<FieldInfo> GetSerializableFields(Type type)
    {
        List<Type> chain = [];
        for (Type? current = type;
             current != null && current != typeof(ScriptBehaviour) && current != typeof(Component);
             current = current.BaseType)
            chain.Add(current);
        chain.Reverse();

        foreach (Type current in chain)
        {
            foreach (FieldInfo field in current.GetFields(
                BindingFlags.Instance | BindingFlags.Public |
                BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (field.IsStatic || field.IsInitOnly) continue;
                if (field.IsPublic || field.GetCustomAttribute<SerializeFieldAttribute>() != null)
                    yield return field;
            }
        }
    }

    //获取宿主字段使用的稳定类型名。
    private static string GetManagedFieldTypeName(Type type, InteropValueKind kind)
    {
        return kind switch
        {
            InteropValueKind.Bool => "bool",
            InteropValueKind.Int32 => "int32",
            InteropValueKind.UInt32 => "uint32",
            InteropValueKind.UInt64 => "uint64",
            InteropValueKind.Float32 => "float32",
            InteropValueKind.String => "string",
            InteropValueKind.Vector3 => "vector3",
            InteropValueKind.Color => "color",
            InteropValueKind.Quaternion => "quaternion",
            InteropValueKind.EnsId => "EnsId",
            InteropValueKind.Object => $"Ref<{type.FullName}>",
            _ => string.Empty,
        };
    }

    //把原生字段分类转换为宿主可恢复的类型名。
    private static string GetNativeFieldTypeName(NativeFieldKind kind)
    {
        return kind switch
        {
            NativeFieldKind.Bool => "bool",
            NativeFieldKind.Int32 => "int32",
            NativeFieldKind.UInt32 => "uint32",
            NativeFieldKind.UInt64 => "uint64",
            NativeFieldKind.Float32 => "float32",
            NativeFieldKind.String => "string",
            NativeFieldKind.StringId => "StringId",
            NativeFieldKind.ObjectRef => "Ref<Orbeden.Object>",
            NativeFieldKind.Vector3 => "vector3",
            NativeFieldKind.Color => "color",
            NativeFieldKind.Quaternion => "quaternion",
            NativeFieldKind.EnsId => "EnsId",
            _ => string.Empty,
        };
    }

    //获取新字段的稳定默认文本。
    private static string GetDefaultSerializedValue(InteropValueKind kind)
    {
        return kind switch
        {
            InteropValueKind.Bool => "false",
            InteropValueKind.Int32 or InteropValueKind.UInt32 or
                InteropValueKind.UInt64 or InteropValueKind.Float32 or
                InteropValueKind.Object => "0",
            InteropValueKind.Vector3 => "0 0 0",
            InteropValueKind.Color or InteropValueKind.Quaternion => "0 0 0 1",
            InteropValueKind.EnsId => $"{uint.MaxValue}:0",
            _ => string.Empty,
        };
    }

    //读取脚本稳定完整类型名。
    private static string GetScriptTypeName(Type type) => type.FullName ?? type.Name;

    //读取便于界面显示的短类型名。
    private static string GetShortTypeName(string typeName)
    {
        int separator = Math.Max(typeName.LastIndexOf('.'), typeName.LastIndexOf('+'));
        return separator >= 0 ? typeName[(separator + 1)..] : typeName;
    }
}
