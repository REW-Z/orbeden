using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using Orbeden;
using NumericsQuaternion = System.Numerics.Quaternion;

namespace OrbedenEditor;

/// <summary>显示并编辑当前 Ens 上的 C++ 与 C# 组件。</summary>
internal sealed class InspectorPanel : EditorPanel
{
    //编辑器侧的会话卸载清理：保存游戏 Type 的静态表必须先清空，否则旧加载上下文回收不掉。
    private static readonly Action SessionCleanup = static () =>
    {
        CustomEditorRegistry.Clear();
        //清完立刻恢复内置编辑器：没有游戏程序集或编译失败时也要能编辑原生组件
        CustomEditorRegistry.RegisterBuiltins();
        EditorObjectField.Clear();
    };

    static InspectorPanel()
    {
        ManagedAssemblySession.RegisterUnloadHandler(SessionCleanup);
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

    //已经按当前脚本程序集补过字段的宿主；程序集每重新加载一次，代次 +1
    private readonly HashSet<(int ObjectId, int Generation)> refreshedManagedHosts = [];
    private int scriptAssemblyGeneration;

    //材质资产面板的缓存：加载到的对象、它绑定的 Shader Key，以及对应的属性文档
    private sealed class MaterialDocument
    {
        internal string Key = string.Empty;
        internal Material? Asset;
        internal string ShaderKey = string.Empty;
        internal PropertyDocument? Document;
    }

    /// <summary>
    /// Inspector 的绘制路径。**只有这三套**：概念可以有很多（组件、材质资产、图片、模型、
    /// 引擎自有格式……），绘制方式不能跟着概念长。
    ///
    /// 新增一种可检视对象时：
    /// - 先归入已有路径。外部原始资源与引擎自有格式的导入产物共用 <see cref="ImportedAsset"/>，
    ///   它们之间的差别（导入设置开不开放）由 EditorAssetInspection 按扩展名决定，不在这里分叉。
    /// - 确实需要新的绘制方式时才加一项，并同步更新 Docs/AssetInspectorDesign.md 的用途对照表。
    ///
    /// 对照 [资源 Inspector 设计](../../../Docs/AssetInspectorDesign.md) 的三种检视用途：
    /// 场景对象、引擎内部资源、外部原始资源。其中后两者的绘制路径是同一条，
    /// 唯一例外是 .orbmat——它是唯一可编辑的资源资产，走 <see cref="EditableAsset"/>。
    /// </summary>
    private enum InspectorView
    {
        /// <summary>没有选中任何可检视目标。</summary>
        None,
        /// <summary>场景对象：Ens 名称、全部组件、Add Component。走 PropertyDocument，可多选与撤销。</summary>
        Ens,
        /// <summary>可编辑的资源资产：目前只有 .orbmat，按绑定的 Shader 展开槽位并写回文本源。</summary>
        EditableAsset,
        /// <summary>导入产物：只读对象清单；导入设置是否开放由 EditorAssetInspection 按扩展名决定。</summary>
        ImportedAsset,
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
    //组件菜单请求的挂载顺序调整：-1 上移、1 下移、0 无请求。
    private int moveRequested;

    private readonly List<Type> scriptTypes = [];
    private string componentSearch = string.Empty;
    private string status = "Game assembly is not loaded.";
    private static string propertyError = string.Empty;
    private sealed class ListState
    {
        internal readonly string DragId = Guid.NewGuid().ToString("N");
        internal int Selected = -1;
    }
    private static readonly ConditionalWeakTable<PropertyDocument, Dictionary<string, ListState>> listStates = new();
    private sealed class EulerRotationState
    {
        internal bool Initialized;
        internal NumericsQuaternion Rotation;
        internal vector3 Degrees;
    }
    private static readonly ConditionalWeakTable<PropertyValue, EulerRotationState> eulerRotations = new();

    //组件卡片右键菜单的弹窗 id 后缀。原生 EditorGuiBeginCollapsibleComponentBlock 用同一个后缀开弹窗，
    //两边算出的 ID 必须一致，改这里就要同步改那边。
    private const string MenuIdSuffix = "##component_menu";

    //组件的启用字段名。有它的组件把勾选框搬到卡片标题行上，正文里不再重复画
    private const string EnabledProperty = "enabled";

    //Ens 卡片的激活字段名。勾选框同样搬到标题行上，正文里不再重复画
    private const string LocalActiveProperty = "LocalActive";

    //Ens 的 static 世界变换约束字段名
    private const string StaticProperty = "Static";

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
        //会话卸载会清空整张回调表（那些回调属于被释放的程序集），本回调属于常驻的编辑器程序集，
        //每次装载游戏程序集时重新登记：只靠静态构造登记一次的话，第一次卸载之后就再也没人清表了，
        //注册表会留着上一个程序集的自定义编辑器，按名字匹配到新类型后画出空白卡片。
        ManagedAssemblySession.RegisterUnloadHandler(SessionCleanup);
        RefreshScriptTypes(assemblyPath);
    }

    /// <summary>卸载 Inspector 缓存的可添加脚本类型。</summary>
    public override void OnGameAssemblyUnloaded()
    {
        ClearScriptTypes();
    }

    /// <summary>C# 字段由 World 的原生宿主统一管理。</summary>
    public override void OnAssetReferencesRemapped(string oldKey, string newKey, bool prefix)
    {
    }

    /// <summary>Inspector 不再保存独立脚本文件。</summary>
    public override bool SavePendingChanges() => true;

    /// <summary>绘制当前选择。选择先归入三条绘制路径之一，再由这里分派。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        //Ens 优先于资源：两者同时被选中时清掉资源选择，回到组件检查
        if (!context.SelectedEns.IsNull && EditorAssetInspection.SourcePath.Length != 0)
            EditorAssetInspection.Select(null);

        switch (ClassifySelection(context))
        {
        case InspectorView.Ens:
            DrawEnsSelection(context);
            break;
        case InspectorView.EditableAsset:
            ClearPropertyDocuments();
            DrawMaterialAsset();
            break;
        case InspectorView.ImportedAsset:
            ClearPropertyDocuments();
            EditorAssetInspection.Draw();
            break;
        default:
            ClearPropertyDocuments();
            EditorGUI.Label("No Ens selected.");
            break;
        }
    }

    //把当前选择归入绘制路径。只做归类，不改动选择状态。
    private static InspectorView ClassifySelection(EditorPanelContext context)
    {
        if (!context.SelectedEns.IsNull) return InspectorView.Ens;
        if (EditorAssetInspection.SourcePath.Length == 0) return InspectorView.None;
        return IsEditableAsset(EditorAssetInspection.SourcePath) ? InspectorView.EditableAsset : InspectorView.ImportedAsset;
    }

    //可编辑的资源资产。目前只有 .orbmat：它按绑定的 Shader 展开槽位，改完写回文本源。
    //新增时先确认它确实需要一套独立绘制方式，而不是能并进导入清单那条路径。
    private static bool IsEditableAsset(string path)
        => Path.GetExtension(path).Equals(".orbmat", StringComparison.OrdinalIgnoreCase);

    //场景对象：名称、组件卡片、Add Component
    private void DrawEnsSelection(EditorPanelContext context)
    {
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

    //从统一会话里缓存可添加的具体 C# 脚本类型。
    //会话本身由 EditorRuntime 负责装载与释放，这里只读它的结果，不再自建加载上下文。
    private void RefreshScriptTypes(string assemblyPath)
    {
        ClearScriptTypes();
        //程序集换了，已有宿主的字段表要按新类型重补一次
        ++scriptAssemblyGeneration;
        refreshedManagedHosts.Clear();

        //核心程序集常驻，其中的托管组件（UI 组件）不依赖游戏程序集是否加载成功。
        CollectScriptTypes(typeof(Ens).Assembly);

        Assembly? gameAssembly = ManagedAssemblySession.GetGameAssembly();
        if (gameAssembly != null)
        {
            CustomEditorRegistry.Register(gameAssembly);
            Assembly? editorAssembly = ManagedAssemblySession.GetEditorAssembly();
            if (editorAssembly != null) CustomEditorRegistry.Register(editorAssembly);
            CollectScriptTypes(gameAssembly);
        }
        scriptTypes.Sort((left, right) =>
            string.Compare(GetScriptTypeName(left), GetScriptTypeName(right), StringComparison.Ordinal));

        if (gameAssembly == null)
        {
            status = string.IsNullOrWhiteSpace(assemblyPath) || !File.Exists(assemblyPath)
                ? "Game assembly is not loaded."
                : "Game assembly load failed: " + Path.GetFileName(assemblyPath);
            return;
        }
        status = $"Loaded: {Path.GetFileName(assemblyPath)}";
    }

    //收集程序集里可添加的托管组件类型；核心与游戏程序集用同一套判据。
    private void CollectScriptTypes(Assembly assembly)
    {
        foreach (Type type in GetLoadableTypes(assembly))
        {
            if (type.IsAbstract || !NativeBindingRuntime.IsManagedScript(type)) continue;
            if (type.GetConstructor([typeof(Ens)]) == null) continue;
            scriptTypes.Add(type);
        }
    }

    //清空类型缓存与组件选择状态；程序集由会话统一释放。
    private void ClearScriptTypes()
    {
        ClearPropertyDocuments();
        addChoices.Clear();
        addChoicesDirty = true;
        scriptTypes.Clear();
        EditorEnumOptions.Clear();
        componentSearch = string.Empty;
        status = "Game assembly is not loaded.";
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
                        //static 由原生侧校验层级，失败时保持原值并让属性文档回滚
                        new DelegatedProperty(StaticProperty, InteropValueKind.Bool,
                            () => InteropValue.From(value.Static),
                            updated =>
                            {
                                if (!updated.TryGet(out bool isStatic)) return InteropStatus.TypeMismatch;
                                return value.Static == isStatic || value.TrySetStatic(isStatic)
                                    ? InteropStatus.Ok : InteropStatus.InvocationFailed;
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
        bool toggledStatic = false;
        bool pendingStatic = false;
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

                //Play 与暂停期间 static 只读，原生 setter 仍是最终保护
                bool staticValue = false;
                bool hasStatic = headerDocument.FindProperty(StaticProperty) is { IsReadable: true, Kind: InteropValueKind.Bool } property
                    && property.Value.TryGet(out staticValue);
                if (hasStatic)
                {
                    EditorGUI.BeginDisabled(EditorApplication.IsPlaying);
                    try
                    {
                        bool edited = staticValue;
                        if (EditorGUI.Checkbox("Static", ref edited))
                        {
                            toggledStatic = true;
                            pendingStatic = edited;
                        }
                    }
                    finally { EditorGUI.EndDisabled(); }
                }
            }
        }
        finally
        {
            EditorGUI.EndComponentBlock();
        }

        //勾选框在标题之后才画出来，改动只能等卡片画完再提交
        if (toggled && hasLocalActive)
            ApplyPropertyToggle(headerDocument, LocalActiveProperty, "Selected Ens", !localActive);
        //Static 同理：写进去由原生校验层级，失败会被属性文档回滚
        if (toggledStatic)
            ApplyPropertyToggle(headerDocument, StaticProperty, "Selected Ens", pendingStatic);
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

        //补字段必须排在文档之前：文档是从原生宿主的字段表读出来的
        RefreshManagedHostFields(selection, components);

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
                void DrawDefault() => DrawPropertyDocument(document.Document, title, primary.IsManaged ? "" : primary.TypeName,
                    hasEnabled ? EnabledProperty : string.Empty, refresh: false);
                ComponentEditorTarget[] targets = components.Select((component, index) => new ComponentEditorTarget(
                    component.ObjectId, selection[index], component.TypeName, component.IsManaged)).ToArray();
                if (!CustomEditorRegistry.DrawInspector(targets, document.Document, DrawDefault)) DrawDefault();
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
        if (moveRequested != 0) MoveComponentGroup(selection, components, moveRequested);
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
        moveRequested = 0;
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
        //挂载顺序决定网格修改器与输入处理器的执行次序，所以顺序要能调。
        EditorGUI.Separator();
        if (EditorGUI.MenuItem("Move Component Up")) moveRequested = -1;
        if (EditorGUI.MenuItem("Move Component Down")) moveRequested = 1;

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
        string skipProperty = "", bool refresh = true)
    {
        if (refresh) document.Update();
        foreach (PropertyValue property in document.GetDrawProperties(nativeType))
        {
            if (!property.IsReadable) continue;
            //元素由所属列表统一绘制，折叠时不在外部重复出现。
            int bracket = property.Name.LastIndexOf('[');
            if (bracket > 0 && property.Name.EndsWith(']')
                && document.FindProperty(property.Name[..bracket])?.Kind == InteropValueKind.Array) continue;
            //挪到卡片标题行上的字段不在正文里再画一遍
            if (skipProperty.Length != 0 && string.Equals(property.Name, skipProperty, StringComparison.Ordinal)) continue;
            //显示名优先用 Label：槽位的标识是 uniform 名，直接展示太生硬
            string title = property.Label.Length != 0 ? property.Label : property.Name;
            string label = property.HasMultipleDifferentValues ? $"{title} (Mixed)" : title;
            InteropValue value;
            //容器字段统一绘制折叠头、元素和结构编辑按钮。
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
            else if (nativeType == "Transform" && property.Name == "localRotation" && property.Kind == InteropValueKind.Quaternion)
            {
                if (!TryDrawEulerRotation(label, property, out value)) continue;
            }
            else if (property.Kind == InteropValueKind.UInt32 && EditorLayerSettings.Handles(nativeType, property.Name))
            {
                if (!EditorLayerSettings.Draw(label, property, out value)) continue;
            }
            else if (!TryDrawProperty(label, property, out value)) continue;
            property.SetValue(value);
        }
        if (document.HasPendingChanges)
            propertyError = document.ApplyChanges($"Edit {undoPrefix}") ? string.Empty
                : $"Failed to apply {undoPrefix}; changes were rolled back.";
    }

    /// <summary>供 CustomEditor 复用单个默认字段控件。</summary>
    internal static void DrawCustomProperty(PropertyDocument document, string name)
    {
        PropertyValue? property = document.FindProperty(name);
        if (property == null || !property.IsReadable) return;
        string label = property.HasMultipleDifferentValues ? name + " (Mixed)" : name;
        if (property.Kind == InteropValueKind.Array) { DrawListSlots(document, property, label); return; }
        bool changed = property.ReferenceType.Length != 0 ? EditorObjectField.Draw(label, property, out InteropValue value)
            : TryDrawProperty(label, property, out value);
        if (changed) property.SetValue(value);
    }

    /// <summary>绘制含增删按钮的列表标题与元素。</summary>
    private static void DrawListSlots(PropertyDocument document, PropertyValue property, string label)
    {
        property.Value.TryGet(out int count);
        Dictionary<string, ListState> states = listStates.GetOrCreateValue(document);
        if (!states.TryGetValue(property.Name, out ListState? state)) states[property.Name] = state = new();
        if (state.Selected >= count) state.Selected = count - 1;
        bool canModify = !EditorApplication.IsPlaying;
        bool canResize = canModify && !property.IsFixedSize;
        int size = count;
        int layout = NativeEditorGUI.BeginList($"{label}###{property.Name}_list", ref size, canResize, property.HasMultipleDifferentValues,
            canResize && count > 0 && !property.HasMultipleDifferentValues);
        int action = 0, moveFrom = -1, moveTo = -1;
        try
        {
            if ((layout & 1) != 0)
            {
                for (int index = 0; index < count; ++index)
                {
                    PropertyValue? element = document.FindProperty($"{property.Name}[{index}]");
                    if (element == null) break;
                    if (NativeEditorGUI.ListElement(index, state.Selected == index)) state.Selected = index;
                    if (canModify)
                    {
                        NativeEditorGUI.DragSource(3, $"{state.DragId}:{index}");
                        string dragged = NativeEditorGUI.ReadDrag(out int kind);
                        if (kind == 3 && dragged.StartsWith(state.DragId + ":", StringComparison.Ordinal)
                            && int.TryParse(dragged.AsSpan(state.DragId.Length + 1), out int source)
                            && source >= 0 && document.FindProperty($"{property.Name}[{source}]") != null)
                        {
                            int placement = NativeEditorGUI.GetDropPlacement() < 0 ? -1 : 1;
                            int destination = index + (placement > 0 ? 1 : 0);
                            if (source < destination) --destination;
                            if (NativeEditorGUI.AcceptDrag(source != destination, placement))
                            { moveFrom = source; moveTo = destination; }
                        }
                    }
                    EditorGUI.TableSetColumnIndex(1);
                    string elementLabel = element.HasMultipleDifferentValues ? $"Mixed##value_{index}" : $"##value_{index}";
                    bool changed = element.ReferenceType.Length != 0
                        ? EditorObjectField.Draw(elementLabel, element, out InteropValue updated)
                        : TryDrawProperty(elementLabel, element, out updated);
                    if (changed) { element.SetValue(updated); state.Selected = index; }
                }
            }
        }
        finally
        {
            action = NativeEditorGUI.EndList();
        }
        if (action == 1 && EditListSlots(document, property.Name, values => values.Add(InteropValue.Empty))) state.Selected = count;
        else if (action == 2)
        {
            int remove = state.Selected < 0 ? count - 1 : state.Selected;
            if (EditListSlots(document, property.Name, values => values.RemoveAt(remove))) state.Selected = Math.Min(remove, count - 2);
        }
        else if (moveFrom >= 0 && EditListSlots(document, property.Name, values =>
        {
            InteropValue moved = values[moveFrom];
            values.RemoveAt(moveFrom);
            values.Insert(moveTo, moved);
        })) state.Selected = moveTo;
    }

    /// <summary>读取元素原始类型和值，撤销时保留空引用和普通数值。</summary>
    private static InteropValue[] ReadListSlots(IPropertyTarget target, string name)
    {
        target.Refresh();
        if (target.TryGet(name, out var value) != InteropStatus.Ok || !value.TryGet(out int count))
            throw new InvalidOperationException("Cannot read list size.");
        InteropValue[] result = new InteropValue[count];
        for (int index = 0; index < count; ++index)
            if (target.TryGet($"{name}[{index}]", out result[index]) != InteropStatus.Ok)
                throw new InvalidOperationException($"Cannot read element {index}.");
        return result;
    }

    /// <summary>先恢复长度并刷新字段结构，再恢复元素；空标记保留新槽默认值。</summary>
    private static void WriteListSlots(IPropertyTarget target, string name, InteropValue[] values)
    {
        target.Refresh();
        if (target.Set(name, InteropValue.FromArray(values.Length)) != InteropStatus.Ok)
            throw new InvalidOperationException("Cannot write list size.");
        target.Refresh();
        for (int index = 0; index < values.Length; ++index)
            if (values[index].Kind != InteropValueKind.Empty && target.Set($"{name}[{index}]", values[index]) != InteropStatus.Ok)
                throw new InvalidOperationException($"Cannot write element {index}.");
        target.Refresh();
    }

    /// <summary>批量应用快照，失败时恢复所有已经写入的目标。</summary>
    private static void ApplyListSlots(IReadOnlyList<IPropertyTarget> targets, string name, IReadOnlyList<InteropValue[]> values)
    {
        InteropValue[][] before = targets.Select(target => ReadListSlots(target, name)).ToArray();
        for (int index = 0; index < targets.Count; ++index)
        {
            try { WriteListSlots(targets[index], name, values[index]); }
            catch (Exception failure)
            {
                List<Exception> failures = [failure];
                for (int rollback = index; rollback >= 0; --rollback)
                {
                    try { WriteListSlots(targets[rollback], name, before[rollback]); }
                    catch (Exception rollbackFailure) { failures.Add(rollbackFailure); }
                }
                throw new AggregateException("List edit failed.", failures);
            }
        }
        foreach (var target in targets) target.MarkDirty();
    }

    /// <summary>提交一次可回滚、可撤销的增删或排序操作。</summary>
    private static bool EditListSlots(PropertyDocument document, string fieldName, Action<List<InteropValue>> edit)
    {
        if (document.HasPendingChanges && !document.ApplyChanges($"Edit {fieldName}"))
        { propertyError = $"Failed to apply {fieldName}."; return false; }
        try
        {
            var targets = document.Targets;
            InteropValue[][] before = targets.Select(target => ReadListSlots(target, fieldName)).ToArray();
            InteropValue[][] after = before.Select(values =>
            {
                List<InteropValue> updated = values.ToList();
                edit(updated);
                return updated.ToArray();
            }).ToArray();
            if (before.Zip(after).All(pair => pair.First.SequenceEqual(pair.Second))) return true;
            ApplyListSlots(targets, fieldName, after);
            //读回新槽默认值，Redo 恢复确切内容。
            after = targets.Select(target => ReadListSlots(target, fieldName)).ToArray();
            EditorPropertyHistory.PushAction($"Edit {fieldName}",
                () => ApplyListSlots(targets, fieldName, before),
                () => ApplyListSlots(targets, fieldName, after));
            propertyError = string.Empty;
            EditorApplication.RequestRepaint();
            return true;
        }
        catch (Exception exception)
        {
            propertyError = $"Failed to edit {fieldName}: {exception.Message}";
            return false;
        }
    }

    /// <summary>以角度显示 Transform 旋转，保留用户输入的欧拉角分支。
    /// 次序是内禀 YXZ（等价于外禀 ZXY，即 R = Ry·Rx·Rz），与 Unity 一致；
    /// 完整说明见 Docs/ProjectConventions.md 的「旋转次序」。</summary>
    private static bool TryDrawEulerRotation(string label, PropertyValue property, out InteropValue value)
    {
        value = property.Value;
        if (!value.TryGet(out quaternion current)) return false;
        NumericsQuaternion rotation = new(current.x, current.y, current.z, current.w);
        float length = rotation.LengthSquared();
        rotation = float.IsFinite(length) && length > 0.000000000001f
            ? NumericsQuaternion.Normalize(rotation) : NumericsQuaternion.Identity;
        EulerRotationState state = eulerRotations.GetOrCreateValue(property);
        const float degreesPerRadian = 180.0f / MathF.PI;

        //只在外部旋转变化时重新分解，避免输入超过 90/180 度时跳到另一组等价角度。
        if (!state.Initialized || ((rotation - state.Rotation).LengthSquared() > 0.000000000001f
            && (rotation + state.Rotation).LengthSquared() > 0.000000000001f))
        {
            float x = rotation.X, y = rotation.Y, z = rotation.Z, w = rotation.W;
            float sinX = Math.Clamp(2.0f * (w * x - y * z), -1.0f, 1.0f);
            float sinRoll = 2.0f * (x * y + w * z);
            float cosRoll = 1.0f - 2.0f * (x * x + z * z);
            float cosX = MathF.Sqrt(sinRoll * sinRoll + cosRoll * cosRoll);
            float pitch = MathF.Atan2(sinX, cosX);
            float yaw, roll;
            if (cosX > 0.000001f)
            {
                yaw = MathF.Atan2(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y));
                roll = MathF.Atan2(sinRoll, cosRoll);
            }
            else
            {
                //俯仰接近直角时固定 Z，保留可确定的 Y 旋转。
                yaw = MathF.Atan2(2.0f * (w * y - x * z), 1.0f - 2.0f * (y * y + z * z));
                roll = 0.0f;
            }
            state.Degrees = new(pitch * degreesPerRadian, yaw * degreesPerRadian, roll * degreesPerRadian);
            state.Rotation = rotation;
            state.Initialized = true;
        }

        vector3 degrees = state.Degrees;
        if (!EditorGUI.InputVector3(label + " (deg)", ref degrees)
            || !float.IsFinite(degrees.x) || !float.IsFinite(degrees.y) || !float.IsFinite(degrees.z)) return false;
        //与原生场景相机保持一致：Y 为 yaw、X 为 pitch，Z 为 roll；内部仍保存四元数。
        rotation = NumericsQuaternion.Normalize(NumericsQuaternion.CreateFromYawPitchRoll(
            (degrees.y % 360.0f) / degreesPerRadian, (degrees.x % 360.0f) / degreesPerRadian, (degrees.z % 360.0f) / degreesPerRadian));
        state.Degrees = degrees;
        state.Rotation = rotation;
        value = InteropValue.From(new quaternion(rotation.X, rotation.Y, rotation.Z, rotation.W));
        return true;
    }

    //绘制单个属性并返回用户提交的新值。
    private static bool TryDrawProperty(string label, PropertyValue property, out InteropValue value)
    {
        if (property.TypeName == "OrbEvent") return OrbEventEditor.Draw(label, property, out value);
        value = property.Value;
        //枚举字段只画下拉框：选项在属性快照重建时解析好，画过就不再走数值控件。
        if (property.Kind is InteropValueKind.Int32 or InteropValueKind.UInt32
            && property.EnumOptions is { Length: > 0 } options)
            return TryDrawEnumSelection(label, property, options, out value);
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
            case InteropValueKind.Vector2:
            {
                property.Value.TryGet(out vector2 current);
                if (!EditorGUI.InputVector2(label, ref current)) return false;
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
                //颜色走共享的颜色字段原语：RGB 与 Alpha 都在拾色器里，粒子那边用的也是这一个
                property.Value.TryGet(out color color);
                if (!GUI.ColorField(label, ref color)) return false;
                value = InteropValue.From(color);
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


    //按快照上解析好的枚举项画下拉框；返回是否有新值。
    private static bool TryDrawEnumSelection(string label, PropertyValue property,
        (long Value, string Label)[] options, out InteropValue value)
    {
        value = property.Value;
        property.Value.TryGet(out uint unsignedValue);
        property.Value.TryGet(out int signedValue);
        bool isUnsigned = property.Kind == InteropValueKind.UInt32;
        long current = isUnsigned ? unsignedValue : signedValue;
        long selected = current;
        if (!EditorGUI.BeginCombo(label, EditorEnumOptions.GetLabel(options, current))) return false;
        try
        {
            foreach ((long option, string optionLabel) in options)
                if (EditorGUI.Selectable(optionLabel, option == current)) selected = option;
        }
        finally { EditorGUI.EndCombo(); }
        if (selected == current) return false;
        value = isUnsigned ? InteropValue.From((uint)selected) : InteropValue.From((int)selected);
        return true;
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

    //脚本改了字段（比如新增 public 字段）并重新 Build Game C# 之后，已有宿主里还没有这些字段：
    //宿主的字段表只在"添加组件"和进 Play 时对过账，存档里的旧表会一直是旧形状。
    //这里按当前类型对一次账——缺失的字段取构造函数里的默认值，类型里已经没有的字段删掉。
    //每个宿主每代只做一次；删掉的字段进撤销，撤销时按组件重新定位宿主再写回。
    private void RefreshManagedHostFields(IReadOnlyList<EnsId> selection, IReadOnlyList<NativeComponentInfo> components)
    {
        for (int index = 0; index < components.Count && index < selection.Count; ++index)
        {
            NativeComponentInfo component = components[index];
            if (!component.IsManaged) continue;
            //先把本代次记掉再解析类型：解析不到（Missing Script、程序集加载失败）就整帧跳过，
            //不再对每个组件每帧重扫一次类型表。类型表变化必然伴随代次递增与集合清空
            //（RefreshScriptTypes），所以这一代次内不重试不会漏掉任何宿主。
            if (!refreshedManagedHosts.Add((component.ObjectId, scriptAssemblyGeneration))) continue;
            Type? type = FindScriptType(component.TypeName);
            //类型解析不到时什么都不做：字段与值原样保留
            if (type == null) continue;
            try
            {
                IReadOnlyList<DroppedScriptField> dropped =
                    EditorNativeComponents.InitializeManagedFields(selection[index], component.ObjectId, type);
                if (dropped.Count != 0) PushDroppedFieldUndo(component.ObjectId, dropped);
            }
            catch (Exception exception)
            {
                //字段补不上时卡片会表现为"没有字段"，这条消息必须留在面板上：只写控制台等于没有
                propertyError = $"Field refresh failed for {component.TypeName}: {exception.Message}";
                Console.Error.WriteLine($"Inspector: managed field refresh failed for {component.TypeName}: {exception.Message}");
            }
        }
    }

    //被删掉的脚本字段进撤销：撤销写回字段与值，重做再删一次。
    //宿主在撤销时按 objectId 重新取：这期间组件可能被删掉又恢复成新的实例。
    private static void PushDroppedFieldUndo(int objectId, IReadOnlyList<DroppedScriptField> dropped)
    {
        EditorPropertyHistory.PushAction(
            "Sync script fields",
            () =>
            {
                IntPtr host = EditorNativeComponents.FindHost(objectId);
                if (host == IntPtr.Zero) return;
                foreach (DroppedScriptField field in dropped)
                    GameScriptRuntime.WriteHostField(host, field.Name, field.TypeName, field.Value);
                TouchWorld();
            },
            () =>
            {
                IntPtr host = EditorNativeComponents.FindHost(objectId);
                if (host == IntPtr.Zero) return;
                foreach (DroppedScriptField field in dropped) GameScriptRuntime.RemoveHostField(host, field.Name);
                TouchWorld();
            });
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

    //把这一组组件在挂载顺序里移动；顺序决定修改器与输入处理器的执行次序。
    private void MoveComponentGroup(
        IReadOnlyList<EnsId> selection,
        IReadOnlyList<NativeComponentInfo> components,
        int delta)
    {
        List<(int ObjectId, int From, int To)> moves = [];
        for (int index = 0; index < components.Count; ++index)
        {
            List<NativeComponentInfo> ordered = EditorNativeComponents.GetComponents(selection[index]);
            int from = ordered.FindIndex(value => value.ObjectId == components[index].ObjectId);
            int to = from + delta;
            if (from < 0 || to < 0 || to >= ordered.Count) continue;
            if (!EditorNativeComponents.MoveComponent(components[index].ObjectId, to)) continue;
            moves.Add((components[index].ObjectId, from, to));
        }
        if (moves.Count == 0) return;

        TouchWorld();
        EditorPropertyHistory.PushAction(
            "Move Component",
            () =>
            {
                //回退按逆序：同一 Ens 上多个组件的目标下标互相影响。
                for (int index = moves.Count - 1; index >= 0; --index)
                    EditorNativeComponents.MoveComponent(moves[index].ObjectId, moves[index].From);
                TouchWorld();
            },
            () =>
            {
                foreach ((int objectId, int _, int to) in moves) EditorNativeComponents.MoveComponent(objectId, to);
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
