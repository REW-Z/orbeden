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

    //材质资产面板的缓存：加载到的对象、它绑定的 Shader Key，以及对应的属性文档
    private sealed class MaterialDocument
    {
        internal string Key = string.Empty;
        internal Material? Asset;
        internal string ShaderKey = string.Empty;
        internal PropertyDocument? Document;
    }

    private readonly MaterialDocument materialDocument = new();
    private bool materialAssetDirty;
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

    //组件的启用字段名。有它的组件把勾选框搬到卡片标题行上，正文里不再重复画
    private const string EnabledProperty = "enabled";

    //Ens 卡片的激活字段名。勾选框同样搬到标题行上，正文里不再重复画
    private const string LocalActiveProperty = "LocalActive";

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
        if (!context.SelectedEns.IsNull && EditorAssetInspection.SourcePath.Length != 0)
            EditorAssetInspection.Select(null);
        if (context.SelectedEns.IsNull && EditorAssetInspection.SourcePath.Length != 0)
        {
            ClearPropertyDocuments();
            //材质资产可以改，其余资产仍然只读
            if (Path.GetExtension(EditorAssetInspection.SourcePath).Equals(".orbmat", StringComparison.OrdinalIgnoreCase))
                DrawMaterialAsset();
            else
                EditorAssetInspection.Draw();
            return;
        }
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

    //绘制材质资产：Shader 引用、绘制队列，加上 Shader 声明的每个槽位一行。
    //槽位表跟着绑定的 Shader 走，所以换 Shader 时属性文档要重建，行数与名字才会跟着变
    private void DrawMaterialAsset()
    {
        EditorGUI.Label(Path.GetFileName(EditorAssetInspection.SourcePath));
        string key = EditorAssetCatalog.Instance.ToResourceKey(EditorAssetInspection.SourcePath);
        if (key.Length == 0) return;

        //换了资源才重新加载对象与文档，之后每帧复用
        if (!string.Equals(materialDocument.Key, key, StringComparison.Ordinal))
        {
            materialDocument.Key = key;
            materialDocument.Asset = EditorAssetCatalog.Instance.Load(typeof(Material), key) as Material;
            materialDocument.ShaderKey = string.Empty;
            materialDocument.Document = null;
        }

        Material? material = materialDocument.Asset;
        if (material == null)
        {
            EditorGUI.Label("Material is not imported yet.");
            return;
        }

        string shaderKey = material.shader?.GetInstanceId() ?? string.Empty;
        if (materialDocument.Document == null
            || !string.Equals(materialDocument.ShaderKey, shaderKey, StringComparison.Ordinal))
        {
            materialDocument.ShaderKey = shaderKey;
            materialDocument.Document = new PropertyDocument([BuildMaterialTarget(material, key)]);
        }

        //Play 中不写资源文件，与其它资源操作一致；只读展示仍然保留
        EditorGUI.BeginDisabled(!EditorAssetsNative.CanModifyAssets());
        try
        {
            DrawPropertyDocument(materialDocument.Document, "Material", "Material");
        }
        finally
        {
            EditorGUI.EndDisabled();
        }

        if (!string.IsNullOrEmpty(propertyError)) EditorGUI.Label(propertyError);
        //写回放在属性文档提交之后：MarkDirty 只在一次成功提交里被调一次
        if (materialAssetDirty)
        {
            materialAssetDirty = false;
            SaveMaterialAsset(material, key);
        }
    }

    //把改过的材质写回源文件，再让导入缓存按新源重建（重建会复用同一个材质对象）
    private void SaveMaterialAsset(Material material, string key)
    {
        if (!EditorAssetsNative.SaveMaterial(material.GetObjectId(), key))
        {
            propertyError = "Failed to save material; see Console for the reason.";
            return;
        }
        propertyError = string.Empty;
        EditorAssetInspection.Invalidate(force: true);
    }

    //按材质与它绑定的 Shader 生成属性行：Shader 引用、绘制队列，加 Shader 声明的每个槽位
    private IPropertyTarget BuildMaterialTarget(Material material, string key)
    {
        List<DelegatedProperty> properties =
        [
            new DelegatedProperty("shader", InteropValueKind.StringId,
                () => InteropValue.FromStringId(material.shader?.GetInstanceId() ?? string.Empty),
                updated =>
                {
                    if (!updated.TryGet(out string value)) return InteropStatus.TypeMismatch;
                    material.SetShader(value);
                    return InteropStatus.Ok;
                },
                "Orbeden.Shader"),
            new DelegatedProperty("overrideDrawQueue", InteropValueKind.Bool,
                () => InteropValue.From(material.overrideDrawQueue),
                updated =>
                {
                    if (!updated.TryGet(out bool value)) return InteropStatus.TypeMismatch;
                    material.overrideDrawQueue = value;
                    return InteropStatus.Ok;
                }),
            new DelegatedProperty("drawQueue", InteropValueKind.UInt32,
                () => InteropValue.From((uint)material.drawQueue),
                updated =>
                {
                    if (!updated.TryGet(out uint value)) return InteropStatus.TypeMismatch;
                    material.drawQueue = (DrawQueue)value;
                    return InteropStatus.Ok;
                }),
        ];

        Shader? shader = material.shader;
        if (shader != null)
        {
            //槽位按 Shader 的声明列出，材质没设过的槽回落到 Shader 的默认值；
            //标识用 uniform 名（唯一），显示名另走 Label——displayName 会剥掉 Color/Texture 后缀，颜色槽与贴图槽本来就同名
            foreach (ShaderColorSlot slot in shader.colorSlots)
            {
                properties.Add(new DelegatedProperty(slot.name, InteropValueKind.Color,
                    () => InteropValue.From(material.GetColor(slot.name, slot.defaultValue)),
                    updated =>
                    {
                        if (!updated.TryGet(out color value)) return InteropStatus.TypeMismatch;
                        material.SetColor(slot.name, value);
                        return InteropStatus.Ok;
                    },
                    Label: slot.displayName));
            }
            foreach (ShaderFloatSlot slot in shader.floatSlots)
            {
                properties.Add(new DelegatedProperty(slot.name, InteropValueKind.Float32,
                    () => InteropValue.From(material.GetFloat(slot.name, slot.defaultValue)),
                    updated =>
                    {
                        if (!updated.TryGet(out float value)) return InteropStatus.TypeMismatch;
                        material.SetFloat(slot.name, value);
                        return InteropStatus.Ok;
                    },
                    Label: slot.displayName));
            }
            foreach (ShaderTextureSlot slot in shader.textureSlots)
            {
                properties.Add(new DelegatedProperty(slot.name, InteropValueKind.StringId,
                    () => InteropValue.FromStringId(material.GetTexture(slot.name)?.GetInstanceId() ?? string.Empty),
                    updated =>
                    {
                        if (!updated.TryGet(out string value)) return InteropStatus.TypeMismatch;
                        material.SetTexture(slot.name, value);
                        return InteropStatus.Ok;
                    },
                    "Orbeden.Texture2D", slot.displayName));
            }
        }

        return new DelegatedPropertyTarget($"material:{key}", properties, () => materialAssetDirty = true);
    }

    //绘制对象名称与运行时身份。卡片默认折叠：平时只是确认选中的是谁，读 Id 细节时才展开。
    private void DrawObjectHeader(Ens active, IReadOnlyList<EnsId> selection, string stableId)
    {
        if (headerDocument == null)
        {
            List<IPropertyTarget> targets = selection
                .Select(Ens.FromId)
                .Where(value => value.IsValid)
                .Select(value => (IPropertyTarget)new DelegatedPropertyTarget(
                    $"ens:{value.Id.id}:{value.Id.version}",
                    [
                        new DelegatedProperty("Name", InteropValueKind.String,
                            () => InteropValue.From(value.Name),
                            updated =>
                            {
                                if (!updated.TryGet(out string name)) return InteropStatus.TypeMismatch;
                                value.Name = name;
                                return InteropStatus.Ok;
                            }),
                        //勾选框读写自身标记，EnsView 的灰显看的是层级生效后的 worldActive
                        new DelegatedProperty(LocalActiveProperty, InteropValueKind.Bool,
                            () => InteropValue.From(value.LocalActive),
                            updated =>
                            {
                                if (!updated.TryGet(out bool active)) return InteropStatus.TypeMismatch;
                                value.LocalActive = active;
                                return InteropStatus.Ok;
                            }),
                    ],
                    EditorApplication.MarkWorldDirty))
                .ToList();
            headerDocument = new PropertyDocument(targets);
        }

        //标题行的勾选框要先拿到当前值，所以文档在标题之前刷新一次：折叠着的卡片也得跟上撤销与重做
        headerDocument.Update();
        PropertyValue? localActiveProperty = headerDocument.FindProperty(LocalActiveProperty);
        //LocalActive 读不出来时不画勾选框，免得给不存在的状态留个能点的空壳
        bool hasLocalActive = localActiveProperty is { IsReadable: true, Kind: InteropValueKind.Bool };
        bool localActive = true;
        if (hasLocalActive) localActiveProperty!.Value.TryGet(out localActive);

        bool toggled = false;
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock("Selected Ens", "Other", "ens_header",
            localActive, false, out toggled);
        try
        {
            if (expanded)
            {
                DrawPropertyDocument(headerDocument, "Ens", "",
                    hasLocalActive ? LocalActiveProperty : string.Empty);
                //Id 用只读输入框显示：与上面的字段同一套行布局，读数比裸标签整齐
                string runtimeId = $"{active.Id.id}:{active.Id.version}";
                EditorGUI.InputText("Runtime Id", ref runtimeId, readOnly: true);
                if (selection.Count > 1) EditorGUI.Label($"Selected: {selection.Count} Ens");
                string stable = string.IsNullOrEmpty(stableId) ? "<none>" : stableId;
                EditorGUI.InputText("Stable Id", ref stable, readOnly: true);
            }
        }
        finally
        {
            EditorGUI.EndComponentBlock();
        }

        //勾选框在标题之后才画出来，改动只能等卡片画完再提交
        if (toggled && hasLocalActive)
            ApplyPropertyToggle(headerDocument, LocalActiveProperty, "Selected Ens", !localActive);
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
        //卡片身份：原生用它 PushID，也是标题右键菜单弹窗 id 的前半段
        string identity = $"component_{primary.IsManaged}_{primary.TypeName}_{occurrence}";

        //标题行上的勾选框要先拿到当前值，所以文档在标题之前就建好并刷新一次：折叠着的卡片也得跟上撤销与重做
        ComponentDocument document = EnsureComponentDocument(components, primary);
        document.Document.Update();
        PropertyValue? enabledProperty = document.Document.FindProperty(EnabledProperty);
        //没有 enabled 字段的组件（Transform）连勾选框都不画，免得给不存在的状态留个能点的空壳
        bool hasEnabled = enabledProperty is { IsReadable: true, Kind: InteropValueKind.Bool };
        bool enabled = true;
        if (hasEnabled) enabledProperty!.Value.TryGet(out enabled);

        string icon = EditorIconCatalog.ForComponent(primary.TypeName, primary.IsManaged);
        bool toggled = false;
        //有 enabled 字段才画标题行的勾选框；Transform 没有，走不带勾选框的那个重载
        bool expanded = hasEnabled
            ? EditorGUI.BeginCollapsibleComponentBlock(title, icon, identity, enabled, out toggled)
            : EditorGUI.BeginCollapsibleComponentBlock(title, icon, identity);
        bool removeRequested = false;
        try
        {
            //菜单必须画在组件块内：原生是在这一层的 ID 栈上开的弹窗，挪到 EndComponentBlock 之后就换了 ID
            if (EditorGUI.BeginPopupContextItem(identity + MenuIdSuffix))
            {
                try { removeRequested = DrawComponentMenu(selection, components); }
                finally { EditorGUI.EndPopup(); }
            }
            if (expanded)
            {
                DrawPropertyDocument(document.Document, title, primary.IsManaged ? "" : primary.TypeName,
                    hasEnabled ? EnabledProperty : string.Empty);
            }
        }
        finally
        {
            EditorGUI.EndComponentBlock();
        }

        //勾选框在标题之后才画出来，改动只能等卡片画完再提交
        if (toggled && hasEnabled) ApplyPropertyToggle(document.Document, EnabledProperty, title, !enabled);
        //删组件同理：菜单是在画这张卡片的过程中打开的，必须等卡片画完再销毁它自己
        if (removeRequested) RemoveComponentGroup(selection, components, title);
    }

    //取出或重建这一组同类型组件的属性文档。标题行的勾选框也读它，所以不分展开与否
    private ComponentDocument EnsureComponentDocument(IReadOnlyList<NativeComponentInfo> components,
        NativeComponentInfo primary)
    {
        bool rebuild = !componentDocuments.TryGetValue(primary.ObjectId, out ComponentDocument? cached)
            || cached.ObjectIds.Length != components.Count;
        for (int index = 0; !rebuild && index < components.Count; ++index)
            rebuild = cached!.ObjectIds[index] != components[index].ObjectId;
        if (!rebuild) return cached!;

        List<IPropertyTarget> targets = components
            .Select(component => (IPropertyTarget)new NativeComponentPropertyTarget(component, EditorApplication.MarkWorldDirty))
            .ToList();
        cached = new ComponentDocument
        {
            ObjectIds = components.Select(component => component.ObjectId).ToArray(),
            Document = new PropertyDocument(targets),
        };
        componentDocuments[primary.ObjectId] = cached;
        return cached;
    }

    //把标题勾选框的改动交给属性文档提交：写入、失败回滚、撤销事务与多选一次改完都由它负责
    private static void ApplyPropertyToggle(PropertyDocument document, string propertyName, string title, bool value)
    {
        PropertyValue? property = document.FindProperty(propertyName);
        if (property == null) return;

        property.SetValue(InteropValue.From(value));
        if (!document.ApplyChanges($"Edit {title}")) propertyError = $"Failed to apply {title}; changes were rolled back.";
        EditorApplication.RequestRepaint();
    }

    //绘制组件卡片标题的右键菜单。返回是否请求删除本组件：菜单是在画卡片的过程中打开的，
    //删除必须由调用方等卡片画完之后再做
    private bool DrawComponentMenu(IReadOnlyList<EnsId> selection, IReadOnlyList<NativeComponentInfo> components)
    {
        bool removeRequested = false;
        NativeComponentInfo primary = components[0];
        if (EditorGUI.MenuItem("Copy Component")) EditorComponentClipboard.Capture(primary, asNew: true);
        if (EditorGUI.MenuItem("Copy Component Values")) EditorComponentClipboard.Capture(primary, asNew: false);
        if (EditorGUI.MenuItem("Paste Component As New", EditorComponentClipboard.CanPasteAsNew))
            PasteComponentAsNew(selection);
        if (EditorGUI.MenuItem("Paste Component Values", EditorComponentClipboard.Matches(primary)))
            PasteComponentValues(components);
        //Transform 是每个 Ens 的骨架，删掉它没有意义，置灰；托管脚本与其余内建组件都可删
        bool removable = primary.IsManaged
            || !string.Equals(primary.TypeName, "Transform", StringComparison.Ordinal);
        if (EditorGUI.MenuItem("Remove Component", removable)) removeRequested = true;

        //Script 的派生类才算脚本：C# 托管宿主与 C++ 脚本都命中，Transform / Camera 这类内建组件不命中
        if (!EditorNativeComponents.MatchesComponentType(primary.ObjectId, "Script")) return removeRequested;
        //托管脚本要程序集里还有这个类型才有文件可去，「Missing Script」置灰；原生组件的类型必然还在
        bool resolvable = !primary.IsManaged || FindScriptType(primary.TypeName) != null;
        EditorGUI.Separator();
        if (EditorGUI.MenuItem("Edit Script", resolvable)
            && !EditorScriptFiles.Edit(primary.TypeName, primary.IsManaged)) status = ScriptFileMissing(primary.TypeName);
        if (EditorGUI.MenuItem("Locate Script", resolvable)
            && !EditorScriptFiles.Locate(primary.TypeName, primary.IsManaged)) status = ScriptFileMissing(primary.TypeName);
        return removeRequested;
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
            EditorStatusBar.Print($"Script type is not available: {entry.TypeName}");
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
            EditorStatusBar.Print($"No matching fields to paste for {GetShortTypeName(entry.TypeName)}.");
            return;
        }
        if (!document.ApplyChanges($"Paste {GetShortTypeName(entry.TypeName)} Values"))
            EditorStatusBar.Print("Paste Component Values failed; changes were rolled back.");
        TouchWorld();
    }

    //生成带语言域标记的组件标题。
    private string GetComponentTitle(NativeComponentInfo component)
    {
        string name = GetShortTypeName(component.TypeName);
        //标不标语言只看"是不是脚本"：内建组件（Transform / Camera 这些）不继承 Script，不标；
        //用户写的 C++ 与 C# 脚本都继承 Script，各自标出来
        if (!EditorNativeComponents.MatchesComponentType(component.ObjectId, "Script")) return name;
        if (!component.IsManaged) return $"[C++] {name}";
        return FindScriptType(component.TypeName) == null ? $"[C#] Missing Script ({name})" : $"[C#] {name}";
    }

    //绘制 PropertyDocument 支持的全部基础值。
    private static void DrawPropertyDocument(PropertyDocument document, string undoPrefix, string nativeType = "",
        string skipProperty = "")
    {
        document.Update();
        foreach (PropertyValue property in document.GetDrawProperties(nativeType))
        {
            if (!property.IsReadable) continue;
            //挪到卡片标题行上的字段不在正文里再画一遍
            if (skipProperty.Length != 0 && string.Equals(property.Name, skipProperty, StringComparison.Ordinal)) continue;
            //显示名优先用 Label：槽位的标识是 uniform 名，直接展示太生硬
            string title = property.Label.Length != 0 ? property.Label : property.Name;
            string label = property.HasMultipleDifferentValues ? $"{title} (Mixed)" : title;
            InteropValue value;
            //容器字段：只画一行槽位头（名字、当前槽位数、增删按钮），元素行由各自的对象框跟在后面
            if (property.Kind == InteropValueKind.Array)
            {
                DrawListSlots(document, property, label);
                continue;
            }
            if (property.ReferenceType.Length != 0)
            {
                if (!EditorObjectField.Draw(label, property, out value))
                {
                    continue;
                }
            }
            else if (property.Kind == InteropValueKind.UInt32 && EditorLayerSettings.Handles(nativeType, property.Name))
            {
                if (!EditorLayerSettings.Draw(label, property, out value)) continue;
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

    //绘制列表字段的槽位头：显示名与当前槽位数，后面跟增删两个按钮
    private static void DrawListSlots(PropertyDocument document, PropertyValue property, string label)
    {
        property.Value.TryGet(out int count);
        //多选且槽位数不同时 label 已经带 (Mixed)，不再叠一个数
        bool mixed = property.HasMultipleDifferentValues;
        EditorGUI.Label(mixed || count == 0 ? label : $"{label} ({count})");

        bool canModify = !EditorApplication.IsPlaying;
        EditorGUI.SameLine();
        EditorGUI.BeginDisabled(!canModify);
        try
        {
            if (EditorGUI.Button("+")) ResizeListSlots(document, property.Name, 1);
        }
        finally { EditorGUI.EndDisabled(); }
        EditorGUI.SameLine();
        EditorGUI.BeginDisabled(!canModify || count == 0);
        try
        {
            if (EditorGUI.Button("-")) ResizeListSlots(document, property.Name, -1);
        }
        finally { EditorGUI.EndDisabled(); }
    }

    //增删列表槽位。改长度是复合操作：先按组件逐个抓住整份槽位内容，撤销时把内容一起写回，
    //否则"减少槽位"的撤销只会恢复个数，被截掉的槽位值就丢了
    private static void ResizeListSlots(PropertyDocument document, string fieldName, int delta)
    {
        List<(IPropertyTarget Target, string[] Keys)> captured = [];
        List<int> applied = [];
        foreach (IPropertyTarget target in document.Targets)
        {
            if (target.TryGet(fieldName, out InteropValue value) != InteropStatus.Ok || !value.TryGet(out int count)) return;
            string[] keys = new string[count];
            for (int index = 0; index < count; ++index)
                if (target.TryGet($"{fieldName}[{index}]", out InteropValue element) == InteropStatus.Ok)
                    element.TryGet(out keys[index]);
            captured.Add((target, keys));
            applied.Add(Math.Max(0, count + delta));
        }

        for (int index = 0; index < applied.Count; ++index)
        {
            if (captured[index].Target.Set(fieldName, InteropValue.FromArray(applied[index])) == InteropStatus.Ok) continue;
            //失败就把已经改过的写回去，不留半改状态
            for (int rollback = index - 1; rollback >= 0; --rollback)
                captured[rollback].Target.Set(fieldName, InteropValue.FromArray(captured[rollback].Keys.Length));
            return;
        }

        EditorPropertyHistory.PushAction($"Resize {fieldName}",
            () =>
            {
                //先补长度再逐个填槽：元素写入不加长列表，槽位得先存在
                foreach ((IPropertyTarget target, string[] keys) in captured)
                {
                    target.Set(fieldName, InteropValue.FromArray(keys.Length));
                    for (int index = 0; index < keys.Length; ++index)
                        target.Set($"{fieldName}[{index}]", InteropValue.FromStringId(keys[index]));
                }
            },
            () =>
            {
                for (int index = 0; index < applied.Count; ++index)
                    captured[index].Target.Set(fieldName, InteropValue.FromArray(applied[index]));
            });
        EditorApplication.RequestRepaint();
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
