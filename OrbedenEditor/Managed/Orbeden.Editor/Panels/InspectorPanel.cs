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
            Assembly runtimeAssembly = typeof(Script).Assembly;
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

    private sealed class ComponentSnapshot
    {
        public EnsId Ens;
        public int ObjectId;
        public int Index;
        public string Xml = string.Empty;
    }
    private sealed class ComponentDocument
    {
        internal int[] ObjectIds = [];
        internal PropertyDocument Document = null!;
    }

    private readonly Dictionary<int, ComponentDocument> componentDocuments = [];
    private readonly List<int> staleDocuments = [];
    private EnsId[] cachedSelection = [];
    private PropertyDocument? headerDocument;
    private readonly List<ComponentAddChoice> addChoices = [];
    private uint addChoicesGeneration;
    private bool addChoicesDirty = true;

    private readonly List<Type> scriptTypes = [];
    private GameAssemblyLoadContext? gameContext;
    private Assembly? gameAssembly;
    private string componentSearch = string.Empty;
    private string status = "Game assembly is not loaded.";
    private static string propertyError = string.Empty;

    //组件卡片右键菜单的弹窗 id 后缀。原生 EditorGuiBeginCollapsibleComponentBlock 用同一个后缀开弹窗，
    //两边算出的 ID 必须一致，改这里就要同步改那边。
    private const string MenuIdSuffix = "##component_menu";

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
    protected override void DrawContent(EditorPanelContext context)
    {
        if (context.SelectedEns.IsNull)
        {
            ClearPropertyDocuments();
            EditorGUI.Label("No Ens selected.");
            return;
        }

        List<EnsId> selection = GetValidSelection(context.SelectedEns, context.SelectedEnsList);
        if (selection.Count == 0)
        {
            ClearPropertyDocuments();
            EditorGUI.Label("Selected Ens is not alive.");
            return;
        }

        if (!selection.SequenceEqual(cachedSelection))
        {
            ClearPropertyDocuments();
            cachedSelection = selection.ToArray();
        }
        Ens active = Ens.FromId(context.SelectedEns);
        DrawObjectHeader(active, selection, context.SelectedStableId);
        if (!string.IsNullOrWhiteSpace(status)) EditorGUI.Label($"C# Assembly: {status}");
        if (propertyError.Length != 0) EditorGUI.Label(propertyError);
        DrawComponents(selection);
        DrawAddComponent(selection);
    }

    //加载用户程序集并缓存可添加的具体 C# 脚本类型。
    private void LoadGameAssembly(string assemblyPath)
    {
        UnloadReflectionAssembly();
        scriptTypes.Clear();
        addChoicesDirty = true;
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
                if (type.IsAbstract || !NativeBindingRuntime.IsManagedScript(type)) continue;
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
            addChoicesDirty = true;
            status = "Game assembly load failed: " + exception.Message;
        }
    }

    //卸载用户程序集并清空类型缓存。
    private void UnloadGameAssembly()
    {
        UnloadReflectionAssembly();
        scriptTypes.Clear();
        addChoicesDirty = true;
        componentSearch = string.Empty;
        status = "Game assembly is not loaded.";
    }

    //卸载仅供 Inspector 反射的可收集程序集上下文。
    private void UnloadReflectionAssembly()
    {
        ClearPropertyDocuments();
        addChoices.Clear();
        addChoicesDirty = true;
        if (gameAssembly != null) NativeBindingRuntime.UnregisterAssembly(gameAssembly);
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
    private void DrawObjectHeader(Ens active, IReadOnlyList<EnsId> selection, string stableId)
    {
        EditorGUI.BeginComponentBlock("Selected Ens");
        try
        {
            if (headerDocument == null)
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
                headerDocument = new PropertyDocument(targets);
            }
            DrawPropertyDocument(headerDocument, "Ens");
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
        List<NativeComponentInfo>[] componentLists = selection.Select(EditorNativeComponents.GetComponents).ToArray();
        List<NativeComponentInfo> primary = componentLists[0];
        staleDocuments.Clear();
        foreach (int id in componentDocuments.Keys)
            if (!primary.Any(component => component.ObjectId == id)) staleDocuments.Add(id);
        foreach (int id in staleDocuments) componentDocuments.Remove(id);
        Dictionary<(string TypeName, bool Managed), int> occurrences = [];
        foreach (NativeComponentInfo component in primary)
        {
            var key = (component.TypeName, component.IsManaged);
            int occurrence = occurrences.TryGetValue(key, out int count) ? count : 0;
            occurrences[key] = occurrence + 1;

            List<NativeComponentInfo> matches = [];
            foreach (List<NativeComponentInfo> target in componentLists)
            {
                NativeComponentInfo match = FindOccurrence(
                    target,
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
            || !string.Equals(primary.TypeName, "Transform", StringComparison.Ordinal);
        //卡片身份：原生用它 PushID，也是标题右键菜单弹窗 id 的前半段
        string identity = $"component_{primary.IsManaged}_{primary.TypeName}_{occurrence}";
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock(
            title,
            EditorIconCatalog.ForComponent(primary.TypeName, primary.IsManaged),
            identity,
            removable,
            out bool removeRequested);
        try
        {
            //菜单必须画在组件块内：原生是在这一层的 ID 栈上开的弹窗，挪到 EndComponentBlock 之后就换了 ID
            if (EditorGUI.BeginPopupContextItem(identity + MenuIdSuffix))
            {
                try { DrawComponentMenu(selection, components); }
                finally { EditorGUI.EndPopup(); }
            }
            if (expanded && !removeRequested)
            {
                bool rebuild = !componentDocuments.TryGetValue(primary.ObjectId, out ComponentDocument? cached)
                    || cached.ObjectIds.Length != components.Count;
                for (int index = 0; !rebuild && index < components.Count; ++index)
                    rebuild = cached!.ObjectIds[index] != components[index].ObjectId;
                if (rebuild)
                {
                    List<IPropertyTarget> targets = components
                        .Select(component => (IPropertyTarget)new NativeComponentPropertyTarget(component, EditorApplication.MarkWorldDirty))
                        .ToList();
                    cached = new ComponentDocument
                    {
                        ObjectIds = components.Select(component => component.ObjectId).ToArray(),
                        Document = new PropertyDocument(targets),
                    };
                    componentDocuments[primary.ObjectId] = cached;
                }
                DrawPropertyDocument(cached!.Document, title, primary.IsManaged ? "" : primary.TypeName);
            }
        }
        finally
        {
            EditorGUI.EndComponentBlock();
        }

        if (removeRequested) RemoveComponentGroup(selection, components, title);
    }

    //绘制组件卡片标题的右键菜单
    private void DrawComponentMenu(IReadOnlyList<EnsId> selection, IReadOnlyList<NativeComponentInfo> components)
    {
        NativeComponentInfo primary = components[0];
        if (EditorGUI.MenuItem("Copy Component")) EditorComponentClipboard.Capture(primary, asNew: true);
        if (EditorGUI.MenuItem("Copy Component Values")) EditorComponentClipboard.Capture(primary, asNew: false);
        if (EditorGUI.MenuItem("Paste Component As New", EditorComponentClipboard.CanPasteAsNew))
            PasteComponentAsNew(selection);
        if (EditorGUI.MenuItem("Paste Component Values", EditorComponentClipboard.Matches(primary)))
            PasteComponentValues(components);

        //Script 的派生类才算脚本：C# 托管宿主与 C++ 脚本都命中，Transform / Camera 这类内建组件不命中
        if (!EditorNativeComponents.MatchesComponentType(primary.ObjectId, "Script")) return;
        //托管脚本要程序集里还有这个类型才有文件可去，「Missing Script」置灰；原生组件的类型必然还在
        bool resolvable = !primary.IsManaged || FindScriptType(primary.TypeName) != null;
        EditorGUI.Separator();
        if (EditorGUI.MenuItem("Edit Script", resolvable)
            && !EditorScriptFiles.Edit(primary.TypeName, primary.IsManaged)) status = ScriptFileMissing(primary.TypeName);
        if (EditorGUI.MenuItem("Locate Script", resolvable)
            && !EditorScriptFiles.Locate(primary.TypeName, primary.IsManaged)) status = ScriptFileMissing(primary.TypeName);
    }

    //脚本在程序集里有，但内容根下找不到对应文件
    private static string ScriptFileMissing(string scriptType)
        => $"Script file not found for {scriptType}. Scripts must live under the content root.";

    //按剪贴板里的类型新建组件，再把复制来的字段值一起填进去
    private void PasteComponentAsNew(IReadOnlyList<EnsId> selection)
    {
        ComponentClipboardEntry? entry = EditorComponentClipboard.Entry;
        if (entry == null || entry.TypeName.Length == 0) return;

        Type? managedType = entry.IsManaged ? FindScriptType(entry.TypeName) : null;
        if (entry.IsManaged && managedType == null)
        {
            status = $"Script type is not available: {entry.TypeName}";
            return;
        }
        string domain = entry.IsManaged ? "[C#]" : "[C++]";
        AddComponentGroup(selection,
            new ComponentAddChoice(entry.TypeName, $"{domain} {GetShortTypeName(entry.TypeName)}", entry.IsManaged, managedType),
            entry.Values);
    }

    //把剪贴板里的字段值覆盖到每个选中对象上的同类型组件
    private void PasteComponentValues(IReadOnlyList<NativeComponentInfo> components)
    {
        ComponentClipboardEntry? entry = EditorComponentClipboard.Entry;
        if (entry == null || entry.Values.Count == 0) return;

        //借 PropertyDocument 走完整的校验、回滚与撤销：它只认多目标共有的、类型一致的字段
        PropertyDocument document = new(components
            .Select(component => (IPropertyTarget)new NativeComponentPropertyTarget(component, EditorApplication.MarkWorldDirty))
            .ToList());
        document.Update();
        int matched = 0;
        foreach ((string name, InteropValueKind kind, InteropValue value) in entry.Values)
        {
            PropertyValue? property = document.FindProperty(name);
            if (property == null || property.Kind != kind || !property.IsReadable) continue;
            property.SetValue(value);
            ++matched;
        }
        if (matched == 0)
        {
            status = $"No matching fields to paste for {GetShortTypeName(entry.TypeName)}.";
            return;
        }
        if (!document.ApplyChanges($"Paste {GetShortTypeName(entry.TypeName)} Values"))
            status = "Paste Component Values failed; changes were rolled back.";
        TouchWorld();
    }

    //生成带语言域标记的组件标题。
    private string GetComponentTitle(NativeComponentInfo component)
    {
        if (!component.IsManaged) return $"[C++] {GetShortTypeName(component.TypeName)}";
        bool missing = FindScriptType(component.TypeName) == null;
        return missing
            ? $"[C#] Missing Script ({GetShortTypeName(component.TypeName)})"
            : $"[C#] {GetShortTypeName(component.TypeName)}";
    }

    //绘制 PropertyDocument 支持的全部基础值。
    private static void DrawPropertyDocument(PropertyDocument document, string undoPrefix, string nativeType = "")
    {
        document.Update();
        foreach (PropertyValue property in document.GetDrawProperties(nativeType))
        {
            if (!property.IsReadable) continue;
            string label = property.HasMultipleDifferentValues
                ? $"{property.Name} (Mixed)"
                : property.Name;
            InteropValue value;
            if (property.ReferenceType.Length != 0)
            {
                if (!EditorObjectField.Draw(label, property, out value))
                {
                    continue;
                }
            }
            else if (property.Kind == InteropValueKind.UInt32 && nativeType.Length != 0
                && property.Name is "drawQueue" or "bodyType" or "shape")
            {
                string[] labels = property.Name == "drawQueue" ? ["Opaque", "Transparent", "Refraction"]
                    : property.Name == "bodyType" ? ["Static", "Dynamic", "Kinematic"] : ["Capsule", "Box"];
                property.Value.TryGet(out uint current);
                uint selected = current;
                if (!EditorGUI.BeginCombo(label, current < labels.Length ? labels[current] : current.ToString())) continue;
                try
                {
                    for (uint index = 0; index < labels.Length; ++index)
                        if (EditorGUI.Selectable(labels[index], index == current)) selected = index;
                }
                finally { EditorGUI.EndCombo(); }
                if (selected == current) continue;
                value = InteropValue.From(selected);
            }
            else if (!TryDrawProperty(label, property, out value)) continue;
            property.SetValue(value);
        }
        if (document.HasPendingChanges)
            propertyError = document.ApplyChanges($"Edit {undoPrefix}") ? string.Empty
                : $"Failed to apply {undoPrefix}; changes were rolled back.";
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
            case InteropValueKind.Quaternion:
            {
                property.Value.TryGet(out quaternion rotation);
                vector3 xyz = new(rotation.x, rotation.y, rotation.z);
                float w = rotation.w;
                bool changed = EditorGUI.InputVector3(label + " XYZ", ref xyz);
                changed |= EditorGUI.InputFloat(label + " W", ref w);
                if (!changed) return false;
                float length = MathF.Sqrt(xyz.x * xyz.x + xyz.y * xyz.y + xyz.z * xyz.z + w * w);
                if (length < 0.000001f) return false;
                value = InteropValue.From(new quaternion(xyz.x / length, xyz.y / length, xyz.z / length, w / length));
                return true;
            }
            case InteropValueKind.Color:
            {
                property.Value.TryGet(out color color);
                vector3 rgb = new(color.r, color.g, color.b);
                float alpha = color.a;
                bool changed = EditorGUI.InputVector3(label + " RGB", ref rgb);
                changed |= EditorGUI.InputFloat(label + " Alpha", ref alpha);
                if (!changed) return false;
                value = InteropValue.From(new color(rgb.x, rgb.y, rgb.z, alpha));
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
        List<ComponentAddChoice> choices = addChoices;
        uint generation = EditorNativeComponents.RegistryGeneration;
        if (addChoicesDirty || generation != addChoicesGeneration)
        {
            choices.Clear();
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

            addChoicesGeneration = generation;
            addChoicesDirty = false;
        }

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
    //initialValues 非空时，把这份字段值回填进新建的组件（粘贴用）；只回填到目标类型上，不碰顺带补齐的依赖组件。
    private void AddComponentGroup(IReadOnlyList<EnsId> selection, ComponentAddChoice choice,
        IReadOnlyList<(string Name, InteropValueKind Kind, InteropValue Value)>? initialValues = null)
    {
        List<ComponentSnapshot> created = [];
        try
        {
            //先验证全部目标和依赖图，任何 Unique 冲突都不修改 World。
            List<ComponentAddChoice> order = [];
            BuildAddOrder(choice, [], [], order);
            foreach (EnsId ens in selection)
            {
                List<NativeComponentInfo> existing = EditorNativeComponents.GetComponents(ens);
                bool unique = choice.ManagedType?.GetCustomAttribute<UniqueComponentAttribute>(true) != null
                    || !choice.IsManaged && choice.TypeName is "Transform" or "StaticMeshRenderer"
                        or "RigidBody" or "CharacterController" or "Camera";
                if (unique && existing.Any(value => value.IsManaged == choice.IsManaged && value.TypeName == choice.TypeName))
                    throw new InvalidOperationException($"Unique component already exists: {choice.TypeName}");
            }
            foreach (EnsId ens in selection)
            {
                foreach (ComponentAddChoice item in order)
                {
                    List<NativeComponentInfo> before = EditorNativeComponents.GetComponents(ens);
                    if (item.TypeName != choice.TypeName && before.Any(value => value.IsManaged == item.IsManaged && value.TypeName == item.TypeName))
                        continue;
                    int objectId = EditorNativeComponents.AddComponentAndGetId(ens, item.TypeName, item.IsManaged);
                    if (objectId == 0 || before.Any(value => value.ObjectId == objectId))
                        throw new InvalidOperationException($"Component add failed: {item.TypeName}");
                    NativeComponentInfo component = new(objectId, item.TypeName, item.IsManaged);
                    ComponentSnapshot snapshot = CaptureComponent(ens, component);
                    created.Add(snapshot);
                    if (item.IsManaged && item.ManagedType != null) EditorNativeComponents.InitializeManagedFields(ens, objectId, item.ManagedType);
                    //回填要排在托管字段初始化之后、快照之前：撤销恢复出来的才是粘贴后的值
                    if (initialValues != null
                        && item.IsManaged == choice.IsManaged && item.TypeName == choice.TypeName)
                        ApplyInitialValues(objectId, item, initialValues);
                    snapshot.Xml = EditorNativeComponents.CaptureComponent(objectId);
                }
            }
        }
        catch (Exception exception)
        {
            RemoveSnapshots(created);
            status = exception.Message;
            return;
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

    //把复制来的字段值写进刚建好的组件；类型对不上的字段跳过
    private static void ApplyInitialValues(int objectId, ComponentAddChoice choice,
        IReadOnlyList<(string Name, InteropValueKind Kind, InteropValue Value)> values)
    {
        NativeComponentPropertyTarget target = new(
            new NativeComponentInfo(objectId, choice.TypeName, choice.IsManaged), EditorApplication.MarkWorldDirty);
        target.Refresh();
        foreach ((string name, _, InteropValue value) in values)
        {
            if (target.Validate(name, value) != InteropStatus.Ok) continue;
            target.Set(name, value);
        }
    }

    /// <summary>验证依赖图并生成依赖优先的创建顺序。</summary>
    private static void BuildAddOrder(ComponentAddChoice choice, HashSet<string> visiting,
        HashSet<string> visited, List<ComponentAddChoice> order)
    {
        string key = $"{choice.IsManaged}:{choice.TypeName}";
        if (visited.Contains(key)) return;
        if (!visiting.Add(key)) throw new InvalidOperationException($"Cyclic component dependency: {choice.TypeName}");
        Type? type = choice.ManagedType;
        if (type != null)
        {
            if (type.IsAbstract || type.ContainsGenericParameters || !typeof(Component).IsAssignableFrom(type))
                throw new InvalidOperationException($"Invalid component type: {type.FullName}");
            foreach (DependsOnComponentAttribute dependency in type.GetCustomAttributes<DependsOnComponentAttribute>(true))
            {
                foreach (Type required in dependency.ComponentTypes)
                {
                    if (required == null) throw new InvalidOperationException("Null component dependency.");
                    bool managed = NativeBindingRuntime.IsManagedScript(required);
                    string name = managed ? GetScriptTypeName(required) : required.Name;
                    if (managed && required.GetConstructor([typeof(Ens)]) == null
                        || !managed && name != "Transform" && !EditorNativeComponents.GetAddableTypes().Contains(name))
                        throw new InvalidOperationException($"Component has no factory: {name}");
                    BuildAddOrder(new(name, name, managed, required), visiting, visited, order);
                }
            }
        }
        visiting.Remove(key);
        visited.Add(key);
        order.Add(choice);
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

    /// <summary>捕获完整组件快照和原挂载位置。</summary>
    private static ComponentSnapshot CaptureComponent(EnsId ens, NativeComponentInfo component)
    {
        return new ComponentSnapshot
        {
            Ens = ens,
            ObjectId = component.ObjectId,
            Index = EditorNativeComponents.GetComponents(ens).FindIndex(value => value.ObjectId == component.ObjectId),
            Xml = EditorNativeComponents.CaptureComponent(component.ObjectId),
        };
    }

    /// <summary>按依赖的逆序移除组件，失败时保留身份以便报告。</summary>
    private static void RemoveSnapshots(IEnumerable<ComponentSnapshot> snapshots)
    {
        foreach (ComponentSnapshot snapshot in snapshots.Reverse())
        {
            if (snapshot.ObjectId == 0) continue;
            if (!EditorNativeComponents.RemoveComponent(snapshot.ObjectId))
                throw new InvalidOperationException("Component removal failed.");
            snapshot.ObjectId = 0;
        }
    }

    /// <summary>按原顺序恢复完整快照；任一恢复失败则回滚本次恢复。</summary>
    private static void RestoreSnapshots(IEnumerable<ComponentSnapshot> values)
    {
        List<ComponentSnapshot> restored = [];
        foreach (ComponentSnapshot snapshot in values)
        {
            int objectId = EditorNativeComponents.RestoreComponent(snapshot.Ens, snapshot.Xml, snapshot.Index);
            if (objectId == 0)
            {
                RemoveSnapshots(restored);
                throw new InvalidOperationException("Component restore failed; restored components were rolled back.");
            }
            snapshot.ObjectId = objectId;
            restored.Add(snapshot);
        }
    }
    //标记 World 并刷新 Inspector；PIE 中 MarkWorldDirty 会自动忽略磁盘 Dirty。
    private static void TouchWorld()
    {
        EditorApplication.MarkWorldDirty();
        EditorApplication.RequestRepaint();
    }

    /// <summary>释放当前选择的绘制文档，不影响 Undo 持有的稳定身份。</summary>
    private void ClearPropertyDocuments()
    {
        componentDocuments.Clear();
        headerDocument = null;
        cachedSelection = [];
    }

    //按完整名称查找 Inspector 已加载的 C# 类型。
    private Type? FindScriptType(string typeName)
    {
        return scriptTypes.FirstOrDefault(type =>
            string.Equals(GetScriptTypeName(type), typeName, StringComparison.Ordinal));
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
