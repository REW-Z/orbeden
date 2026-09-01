using System.Globalization;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
using System.Text.Json.Serialization;
using Orbeden;

namespace OrbedenEditor;

/// <summary>显示当前 Ens 及其原生和脚本组件。</summary>
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
            if (assemblyName.Name == runtimeAssembly.GetName().Name)
            {
                return runtimeAssembly;
            }

            string? path = resolver.ResolveAssemblyToPath(assemblyName);
            return path != null ? LoadAssemblyFile(path) : null;
        }
    }

    private sealed class ScriptSidecarDocument
    {
        [JsonPropertyName("scripts")]
        public List<ScriptMount> Scripts { get; set; } = [];
    }

    private sealed class ScriptMount
    {
        [JsonPropertyName("id")]
        public string Id { get; set; } = string.Empty;

        [JsonPropertyName("stableId")]
        public string StableId { get; set; } = string.Empty;

        [JsonPropertyName("type")]
        public string Type { get; set; } = string.Empty;

        [JsonPropertyName("enabled")]
        public bool Enabled { get; set; } = true;

        [JsonPropertyName("values")]
        public Dictionary<string, ScriptSerializedValue> Values { get; set; } = [];
    }

    private sealed class ScriptSerializedValue
    {
        [JsonPropertyName("type")]
        public string Type { get; set; } = string.Empty;

        [JsonPropertyName("value")]
        public string Value { get; set; } = string.Empty;
    }

    private readonly record struct ComponentAddChoice(string Key, string Label, Type? ManagedType, string? NativeType);

    private readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private readonly List<Type> scriptTypes = [];
    private readonly Dictionary<string, List<ScriptMount>> sidecarScripts = [];
    private readonly Dictionary<string, string> componentSearches = [];
    private readonly Dictionary<string, string> selectedAddTypes = [];
    private readonly HashSet<ScriptBehaviour> runtimeSerializedApplied = [];
    private GameAssemblyLoadContext? gameContext;
    private Assembly? gameAssembly;
    private string status = "Game assembly is not loaded.";
    private string currentSidecarPath = string.Empty;
    private bool generatedMissingMountIds;
    private static readonly HashSet<string> NativeTypesCoveredByDedicatedDrawers =
    [
        "TransformComponent",
        "StaticMeshRenderer",
        "RigidBodyComponent",
        "ColliderComponent",
        "BoxColliderComponent",
        "SphereColliderComponent",
        "CapsuleColliderComponent",
        "ConvexMeshColliderComponent",
        "TriangleMeshColliderComponent",
        "CharacterControllerComponent",
    ];

    public override EditorPanelInfo Info => new(
        "inspector",
        "Inspector",
        true,
        new vector2(360.0f, 520.0f),
        PanelDockPlacement.Right,
        0.25f,
        300);

    /// <summary>加载 Inspector 使用的游戏程序集和 sidecar。</summary>
    public override void OnGameAssemblyLoaded(string assemblyPath, string sidecarPath)
    {
        LoadGameAssembly(assemblyPath, sidecarPath);
    }

    /// <summary>卸载 Inspector 使用的游戏程序集和 sidecar。</summary>
    public override void OnGameAssemblyUnloaded()
    {
        UnloadGameAssembly();
    }

    /// <summary>同步 sidecar 缓存中的资源对象引用。</summary>
    public override void OnAssetReferencesRemapped(string oldKey, string newKey, bool prefix)
    {
        foreach (List<ScriptMount> mounts in sidecarScripts.Values)
        {
            foreach (ScriptMount mount in mounts)
            {
                foreach (ScriptSerializedValue value in mount.Values.Values)
                {
                    if (!IsSerializedResourceType(value.Type)) continue;
                    if (TryMapResourceKey(value.Value, oldKey, newKey, prefix, out string mapped)) value.Value = mapped;
                }
            }
        }

        runtimeSerializedApplied.Clear();
    }

    /// <summary>绘制 Inspector 内容。</summary>
    public override void Draw(EditorPanelContext context)
    {
        DrawContent(context.SelectedEns, context.SelectedStableId);
    }

    /// <summary>加载用户游戏程序集和脚本挂载清单。</summary>
    private void LoadGameAssembly(string assemblyPath, string sidecarPath)
    {
        UnloadReflectionAssembly();
        scriptTypes.Clear();
        sidecarScripts.Clear();
        componentSearches.Clear();
        selectedAddTypes.Clear();
        runtimeSerializedApplied.Clear();
        generatedMissingMountIds = false;
        currentSidecarPath = sidecarPath;

        LoadSidecar(sidecarPath);
        if (generatedMissingMountIds) SaveSidecar();
        if (string.IsNullOrWhiteSpace(assemblyPath) || !File.Exists(assemblyPath))
        {
            status = sidecarScripts.Count > 0
                ? "Game assembly is not loaded. Showing sidecar script components."
                : "Game assembly is not loaded.";
            return;
        }

        try
        {
            gameContext = new GameAssemblyLoadContext(assemblyPath);
            gameAssembly = gameContext.LoadAssemblyFile(Path.GetFullPath(assemblyPath));
            foreach (Type type in GetLoadableTypes(gameAssembly))
            {
                if (!type.IsAbstract && typeof(ScriptBehaviour).IsAssignableFrom(type))
                {
                    scriptTypes.Add(type);
                }
            }

            scriptTypes.Sort((left, right) => string.Compare(GetScriptTypeName(left), GetScriptTypeName(right), StringComparison.Ordinal));
            status = $"Loaded: {Path.GetFileName(assemblyPath)}";
        }
        catch (Exception ex)
        {
            UnloadReflectionAssembly();
            status = "Game assembly load failed: " + ex.Message;
        }
    }

    /// <summary>清空用户游戏程序集引用和运行态脚本记录。</summary>
    private void UnloadGameAssembly()
    {
        UnloadReflectionAssembly();
        scriptTypes.Clear();
        sidecarScripts.Clear();
        componentSearches.Clear();
        selectedAddTypes.Clear();
        runtimeSerializedApplied.Clear();
        generatedMissingMountIds = false;
        currentSidecarPath = string.Empty;
        ScriptRuntimeRegistry.Clear();
        status = "Game assembly is not loaded.";
    }

    //绘制选中 Ens 的 Inspector 内容。
    private void DrawContent(EnsId selectedEns, string stableId)
    {
        if (selectedEns.IsNull)
        {
            EditorGUI.Label("No Ens selected.");
            return;
        }

        Ens ens = Ens.FromId(selectedEns);
        if (!ens.IsValid)
        {
            EditorGUI.Label("Selected Ens is not alive.");
            return;
        }

        DrawObjectHeader(ens, selectedEns, stableId);
        DrawInspectorStatus();
        DrawManagedComponents(ens, selectedEns, stableId);
    }

    //绘制选中对象摘要。
    private void DrawObjectHeader(Ens ens, EnsId selectedEns, string stableId)
    {
        DrawComponentBlock("Selected Ens", () =>
        {
            string name = ens.Name;
            if (EditorGUI.InputText("Name", ref name))
            {
                ens.Name = name;
                EditorApplication.RequestRepaint();
            }

            EditorGUI.Label($"Runtime Id: {selectedEns.id}:{selectedEns.version}");
            EditorGUI.Label(string.IsNullOrEmpty(stableId) ? "Stable Id: <none>" : $"Stable Id: {stableId}");
        });
    }

    //绘制 Inspector 当前脚本程序集状态。
    private void DrawInspectorStatus()
    {
        if (string.IsNullOrWhiteSpace(status)) return;

        EditorGUI.Label($"C# Assembly: {status}");
    }

    //卸载仅用于 Inspector 反射的用户程序集上下文。
    private void UnloadReflectionAssembly()
    {
        gameAssembly = null;
        if (gameContext == null) return;

        gameContext.Unload();
        gameContext = null;
    }

    //读取可加载类型，忽略坏类型。
    private IEnumerable<Type> GetLoadableTypes(Assembly assembly)
    {
        try
        {
            return assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException ex)
        {
            return ex.Types.Where(type => type != null)!;
        }
    }

    //读取 world sidecar 脚本挂载清单。
    private void LoadSidecar(string sidecarPath)
    {
        if (string.IsNullOrWhiteSpace(sidecarPath) || !File.Exists(sidecarPath)) return;

        try
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(sidecarPath));
            if (!document.RootElement.TryGetProperty("scripts", out JsonElement scripts)) return;
            if (scripts.ValueKind != JsonValueKind.Array) return;

            foreach (JsonElement element in scripts.EnumerateArray())
            {
                ScriptMount? mount = ReadScriptMount(element);
                if (mount == null) continue;

                if (!sidecarScripts.TryGetValue(mount.StableId, out List<ScriptMount>? mounts))
                {
                    mounts = [];
                    sidecarScripts.Add(mount.StableId, mounts);
                }

                mounts.Add(mount);
            }
        }
        catch (Exception ex)
        {
            status = "Script sidecar load failed: " + ex.Message;
        }
    }

    //读取单个 sidecar 脚本挂载项。
    private ScriptMount? ReadScriptMount(JsonElement element)
    {
        string? stableId = element.TryGetProperty("stableId", out JsonElement stableIdElement) ? stableIdElement.GetString() : null;
        string? type = element.TryGetProperty("type", out JsonElement typeElement) ? typeElement.GetString() : null;
        if (string.IsNullOrWhiteSpace(stableId) || string.IsNullOrWhiteSpace(type)) return null;

        string? id = element.TryGetProperty("id", out JsonElement idElement) ? idElement.GetString() : null;
        if (string.IsNullOrWhiteSpace(id))
        {
            id = Guid.NewGuid().ToString("N");
            generatedMissingMountIds = true;
        }

        bool enabled = !element.TryGetProperty("enabled", out JsonElement enabledElement)
            || enabledElement.ValueKind != JsonValueKind.False;
        ScriptMount mount = new() { Id = id, StableId = stableId, Type = StripAssemblyName(type), Enabled = enabled };
        if (!element.TryGetProperty("values", out JsonElement values)) return mount;
        if (values.ValueKind != JsonValueKind.Object) return mount;

        foreach (JsonProperty property in values.EnumerateObject())
        {
            mount.Values[property.Name] = ReadSerializedValue(property.Value);
        }

        return mount;
    }

    //读取一个序列化字段值。
    private ScriptSerializedValue ReadSerializedValue(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object && element.TryGetProperty("value", out JsonElement valueElement))
        {
            string type = element.TryGetProperty("type", out JsonElement typeElement) ? typeElement.GetString() ?? string.Empty : string.Empty;
            return new ScriptSerializedValue { Type = type, Value = GetJsonValueText(valueElement) };
        }

        return new ScriptSerializedValue { Value = GetJsonValueText(element) };
    }

    //把 JsonElement 转成可编辑文本。
    private string GetJsonValueText(JsonElement element)
    {
        return element.ValueKind == JsonValueKind.String ? element.GetString() ?? string.Empty : element.GetRawText();
    }

    //保存 world sidecar 脚本挂载清单。
    private void SaveSidecar()
    {
        if (string.IsNullOrWhiteSpace(currentSidecarPath)) return;

        try
        {
            string? directory = Path.GetDirectoryName(currentSidecarPath);
            if (!string.IsNullOrEmpty(directory))
            {
                Directory.CreateDirectory(directory);
            }

            ScriptSidecarDocument document = new()
            {
                Scripts = sidecarScripts.Values.SelectMany(mounts => mounts).ToList(),
            };
            File.WriteAllText(currentSidecarPath, JsonSerializer.Serialize(document, JsonOptions));
        }
        catch (Exception ex)
        {
            status = "Script sidecar save failed: " + ex.Message;
        }
    }

    //绘制引擎 Bind 对应的 C# 组件块。
    private void DrawBoundComponents(Ens ens, EnsId selectedEns)
    {
        if (ens.HasTransformComponent)
        {
            DrawCollapsibleComponentBlock("TransformComponent", $"bound_{selectedEns.id}_TransformComponent", false, () =>
            {
                TransformComponent transform = ens.Transform;
                vector3 localPosition = transform.localPosition;
                if (EditorGUI.InputVector3("localPosition", ref localPosition))
                {
                    transform.localPosition = localPosition;
                    EditorApplication.RequestRepaint();
                }

                vector3 localScale = transform.localScale;
                if (EditorGUI.InputVector3("localScale", ref localScale))
                {
                    transform.localScale = localScale;
                    EditorApplication.RequestRepaint();
                }

                quaternion localRotation = transform.localRotation;
                EditorGUI.Label($"localRotation: {FormatQuaternion(localRotation)}");
                EditorGUI.Label($"worldPosition: {SerializeVector3(transform.worldPosition)}");
                EditorGUI.Label($"worldRotation: {FormatQuaternion(transform.worldRotation)}");
            });
        }

        if (ens.HasStaticMeshRenderer)
        {
            StaticMeshRenderer? renderer = ens.GetComponent<StaticMeshRenderer>();
            if (renderer != null && DrawCollapsibleComponentBlock("StaticMeshRenderer", $"bound_{selectedEns.id}_StaticMeshRenderer", true, () =>
            {
                bool enabled = renderer.enabled;
                if (EditorGUI.Checkbox("enabled", ref enabled))
                {
                    renderer.enabled = enabled;
                    EditorApplication.RequestRepaint();
                }

                Mesh? mesh = renderer.mesh;
                if (EditorGUI.ObjectField("mesh", ref mesh))
                {
                    renderer.mesh = mesh;
                    EditorApplication.RequestRepaint();
                }

                if (mesh != null && mesh.IsValid)
                {
                    EditorGUI.Label($"mesh stats: {mesh.vertexCount} vertices, {mesh.indexCount} indices, {mesh.subMeshCount} subMeshes");
                }

                DrawQueue drawQueue = renderer.drawQueue;
                if (EditorGUI.BeginCombo("drawQueue", drawQueue.ToString()))
                {
                    try
                    {
                        foreach (DrawQueue queue in Enum.GetValues<DrawQueue>())
                        {
                            if (!EditorGUI.Selectable(queue.ToString(), queue == drawQueue)) continue;

                            renderer.drawQueue = queue;
                            drawQueue = queue;
                            EditorApplication.RequestRepaint();
                        }
                    }
                    finally
                    {
                        EditorGUI.EndCombo();
                    }
                }

                bool castShadows = renderer.castShadows;
                if (EditorGUI.Checkbox("castShadows", ref castShadows))
                {
                    renderer.castShadows = castShadows;
                    EditorApplication.RequestRepaint();
                }

                bool receiveShadows = renderer.receiveShadows;
                if (EditorGUI.Checkbox("receiveShadows", ref receiveShadows))
                {
                    renderer.receiveShadows = receiveShadows;
                    EditorApplication.RequestRepaint();
                }
            }))
            {
                Orbeden.Object.Destroy(renderer);
                EditorApplication.RequestRepaint();
            }
        }

        DrawBoundComponent(ens.GetComponent<RigidBody>(), selectedEns, component => DrawScriptMembers(component, typeof(RigidBody)));
        Collider[] colliders = ens.GetComponents<Collider>();
        for (int index = 0; index < colliders.Length; index++)
        {
            Collider collider = colliders[index];
            DrawBoundCollider(collider, selectedEns, index);
        }
        DrawBoundComponent(ens.GetComponent<CharacterController>(), selectedEns, component => DrawScriptMembers(component, typeof(CharacterController)));
    }

    //绘制一个可移除的引擎 Bind 组件。
    private void DrawBoundComponent<T>(T? component, EnsId selectedEns, Action<T> draw) where T : Component
    {
        if (component == null) return;

        string typeName = typeof(T).Name;
        if (DrawCollapsibleComponentBlock(typeName, $"bound_{selectedEns.id}_{typeName}", true, () => draw(component)))
        {
            Orbeden.Object.Destroy(component);
            EditorApplication.RequestRepaint();
        }
    }

    //绘制一个可独立删除的 Collider 实例。
    private void DrawBoundCollider(Collider collider, EnsId selectedEns, int index)
    {
        string typeName = collider.GetType().Name;
        if (DrawCollapsibleComponentBlock(typeName, $"bound_{selectedEns.id}_{typeName}_{index}", true, () =>
        {
            DrawScriptMembers(collider, typeof(Collider));
            DrawScriptMembers(collider, collider.GetType());
        }))
        {
            Orbeden.Object.Destroy(collider);
            EditorApplication.RequestRepaint();
        }
    }

    //绘制原生组件和用户 C# 脚本组成的组件列表。
    private void DrawManagedComponents(Ens ens, EnsId selectedEns, string stableId)
    {
        List<ScriptMount>? mounts = null;
        if (!string.IsNullOrEmpty(stableId)) sidecarScripts.TryGetValue(stableId, out mounts);
        mounts ??= [];
        IReadOnlyList<ScriptBehaviour> runtimeScripts = ScriptRuntimeRegistry.GetScripts(selectedEns);
        HashSet<ScriptBehaviour> drawnRuntimeScripts = [];

        List<NativeComponentInfo> nativeComponents = EditorNativeComponents.GetComponents(selectedEns);
        EditorGUI.Label($"Components ({nativeComponents.Count + mounts.Count})");
        DrawBoundComponents(ens, selectedEns);
        DrawGenericNativeComponents(nativeComponents, selectedEns);
        if (string.IsNullOrEmpty(stableId))
        {
            EditorGUI.Label("Selected Ens has no stableId. C# script components require a stableId.");
            DrawComponentBlock("Add Component", () => DrawAddComponentControls(ens, selectedEns, stableId, mounts));
            return;
        }

        int removeIndex = -1;
        for (int index = 0; index < mounts.Count; index++)
        {
            ScriptMount mount = mounts[index];
            string title = GetShortTypeName(mount.Type);
            List<ScriptBehaviour> matchingRuntimeScripts = runtimeScripts
                .Where(script => string.Equals(script.MountId, mount.Id, StringComparison.Ordinal))
                .ToList();
            foreach (ScriptBehaviour script in matchingRuntimeScripts)
            {
                drawnRuntimeScripts.Add(script);
                ApplySerializedValuesToRuntimeScript(script, stableId);
            }

            bool removeRequested = DrawCollapsibleComponentBlock(title, $"script_{stableId}_{mount.Id}", true, () =>
            {
                EditorGUI.Label($"Type: {mount.Type}");
                bool enabled = mount.Enabled;
                if (EditorGUI.Checkbox("enabled", ref enabled))
                {
                    mount.Enabled = enabled;
                    foreach (ScriptBehaviour script in matchingRuntimeScripts) script.enabled = enabled;
                    SaveSidecar();
                    EditorApplication.RequestRepaint();
                }
                EditorGUI.Label("Serialized Fields");
                DrawSerializedScriptFields(mount);
                DrawMatchingRuntimeScripts(matchingRuntimeScripts);
            });
            if (removeRequested) removeIndex = index;
        }

        if (removeIndex >= 0)
        {
            mounts.RemoveAt(removeIndex);
            if (mounts.Count == 0)
            {
                sidecarScripts.Remove(stableId);
            }

            SaveSidecar();
            EditorApplication.RequestRepaint();
            return;
        }

        DrawUnmatchedRuntimeScripts(stableId, runtimeScripts, drawnRuntimeScripts);
        DrawComponentBlock("Add Component", () => DrawAddComponentControls(ens, selectedEns, stableId, mounts));
    }

    //绘制没有专用 C# binding 绘制器的原生组件。
    private void DrawGenericNativeComponents(IReadOnlyList<NativeComponentInfo> components, EnsId selectedEns)
    {
        foreach (NativeComponentInfo component in components)
        {
            if (NativeTypesCoveredByDedicatedDrawers.Contains(component.TypeName)) continue;

            bool removeRequested = DrawCollapsibleComponentBlock(
                $"{component.TypeName} [C++]",
                $"native_{selectedEns.id}_{component.ObjectId}",
                true,
                () => DrawGenericNativeFields(component));
            if (!removeRequested) continue;

            if (EditorNativeComponents.RemoveComponent(component.ObjectId)) EditorApplication.RequestRepaint();
            return;
        }
    }

    //按原生反射元数据绘制一个组件的字段。
    private void DrawGenericNativeFields(NativeComponentInfo component)
    {
        int fieldCount = EditorNativeComponents.GetFieldCount(component.ObjectId);
        if (fieldCount == 0)
        {
            EditorGUI.Label("No reflected fields.");
            return;
        }

        for (int fieldIndex = 0; fieldIndex < fieldCount; fieldIndex++)
        {
            string name = EditorNativeComponents.GetFieldName(component.ObjectId, fieldIndex);
            NativeFieldKind kind = EditorNativeComponents.GetFieldKind(component.ObjectId, fieldIndex);
            string value = EditorNativeComponents.GetFieldValue(component.ObjectId, fieldIndex);
            DrawGenericNativeField(component.ObjectId, fieldIndex, name, kind, value);
        }
    }

    //绘制并写回一个原生反射字段。
    private void DrawGenericNativeField(int objectId, int fieldIndex, string name, NativeFieldKind kind, string value)
    {
        string label = $"{name}##native_field_{objectId}_{fieldIndex}";
        string updatedValue = value;
        bool changed = false;
        if (kind == NativeFieldKind.Bool)
        {
            bool typedValue = value is "true" or "1";
            changed = EditorGUI.Checkbox(label, ref typedValue);
            updatedValue = typedValue ? "true" : "false";
        }
        else if (kind == NativeFieldKind.Int32)
        {
            int typedValue = int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int parsed) ? parsed : 0;
            changed = EditorGUI.InputInt(label, ref typedValue);
            updatedValue = typedValue.ToString(CultureInfo.InvariantCulture);
        }
        else if (kind == NativeFieldKind.Float32)
        {
            float typedValue = float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out float parsed) ? parsed : 0.0f;
            changed = EditorGUI.InputFloat(label, ref typedValue);
            updatedValue = typedValue.ToString(CultureInfo.InvariantCulture);
        }
        else if (kind == NativeFieldKind.Vector3)
        {
            vector3 typedValue = ParseVector3(value);
            changed = EditorGUI.InputVector3(label, ref typedValue);
            updatedValue = SerializeVector3(typedValue);
        }
        else if (kind != NativeFieldKind.Unsupported)
        {
            changed = EditorGUI.InputText(label, ref updatedValue);
        }
        else
        {
            EditorGUI.Label($"{name}: unsupported");
        }

        if (changed && EditorNativeComponents.SetFieldValue(objectId, fieldIndex, updatedValue))
        {
            EditorApplication.RequestRepaint();
        }
    }

    //绘制新增 C++ / C# 组件控件。
    private void DrawAddComponentControls(Ens ens, EnsId selectedEns, string stableId, List<ScriptMount> mounts)
    {
        List<ComponentAddChoice> choices = [];
        foreach (Type type in GetAvailableComponentTypes(ens, stableId, mounts))
        {
            string typeName = GetScriptTypeName(type);
            bool nativeBinding = IsNativeComponentType(type);
            choices.Add(new ComponentAddChoice(
                $"managed:{typeName}",
                $"[{(nativeBinding ? "C++" : "C#")}] {GetShortTypeName(typeName)}",
                type,
                null));
        }
        foreach (string typeName in EditorNativeComponents.GetAddableTypes())
        {
            if (NativeTypesCoveredByDedicatedDrawers.Contains(typeName)) continue;
            choices.Add(new ComponentAddChoice($"native:{typeName}", $"[C++] {typeName}", null, typeName));
        }
        choices.Sort((left, right) => string.Compare(left.Label, right.Label, StringComparison.Ordinal));

        if (choices.Count == 0)
        {
            EditorGUI.Label("No component types are available.");
            return;
        }

        string search = componentSearches.TryGetValue(stableId, out string? searchValue) ? searchValue : string.Empty;
        string selectedTypeName = selectedAddTypes.TryGetValue(stableId, out string? selectedValue) ? selectedValue : string.Empty;
        if (!choices.Any(choice => string.Equals(choice.Key, selectedTypeName, StringComparison.Ordinal)))
        {
            selectedTypeName = string.Empty;
            selectedAddTypes.Remove(stableId);
        }

        ComponentAddChoice selectedChoice = choices.FirstOrDefault(choice => string.Equals(choice.Key, selectedTypeName, StringComparison.Ordinal));
        string preview = string.IsNullOrEmpty(selectedChoice.Key) ? "Select a component" : selectedChoice.Label;
        if (EditorGUI.BeginCombo($"Component##add_component_combo_{stableId}", preview))
        {
            try
            {
                if (EditorGUI.InputText($"Search##add_component_search_{stableId}", ref search))
                {
                    componentSearches[stableId] = search;
                }

                bool hasMatch = false;
                foreach (ComponentAddChoice choice in choices)
                {
                    if (!MatchesComponentSearch(choice.Label, search)) continue;

                    hasMatch = true;
                    bool selected = string.Equals(choice.Key, selectedTypeName, StringComparison.Ordinal);
                    if (EditorGUI.Selectable($"{choice.Label}##add_component_{stableId}_{choice.Key}", selected))
                    {
                        selectedTypeName = choice.Key;
                        selectedAddTypes[stableId] = choice.Key;
                    }
                }

                if (!hasMatch) EditorGUI.Label("No matching components.");
            }
            finally
            {
                EditorGUI.EndCombo();
            }
        }

        if (EditorGUI.Button($"Add Component##add_component_button_{stableId}") && !string.IsNullOrEmpty(selectedTypeName))
        {
            selectedChoice = choices.FirstOrDefault(choice => string.Equals(choice.Key, selectedTypeName, StringComparison.Ordinal));
            if (selectedChoice.ManagedType != null) AddComponent(ens, stableId, selectedChoice.ManagedType);
            else if (selectedChoice.NativeType != null && EditorNativeComponents.AddComponent(selectedEns, selectedChoice.NativeType))
                EditorApplication.RequestRepaint();
            componentSearches[stableId] = string.Empty;
            selectedAddTypes.Remove(stableId);
        }
    }

    //获取当前可添加的引擎 Bind 与游戏脚本组件类型。
    private List<Type> GetAvailableComponentTypes(Ens ens, string stableId, List<ScriptMount> mounts)
    {
        List<Type> types =
        [
            typeof(StaticMeshRenderer),
            typeof(RigidBody),
            typeof(BoxCollider),
            typeof(SphereCollider),
            typeof(CapsuleCollider),
            typeof(ConvexMeshCollider),
            typeof(TriangleMeshCollider),
            typeof(CharacterController),
        ];
        if (!string.IsNullOrEmpty(stableId))
        {
            types.AddRange(scriptTypes);
        }

        types.RemoveAll(type => IsUniqueComponent(type) && HasComponentInstance(ens, stableId, mounts, type));
        types.Sort((left, right) => string.Compare(GetScriptTypeName(left), GetScriptTypeName(right), StringComparison.Ordinal));
        return types;
    }

    //按组件规则添加引擎 Bind 或游戏脚本组件。
    private void AddComponent(Ens ens, string stableId, Type type)
    {
        try
        {
            List<Type> order = [];
            BuildComponentAddOrder(type, [], [], order);
            bool sidecarChanged = false;
            foreach (Type componentType in order)
            {
                bool isRequestedType = componentType == type;
                bool hasExisting = HasComponentInstance(ens, stableId, GetScriptMounts(stableId), componentType);
                if (hasExisting && (!isRequestedType || IsUniqueComponent(componentType))) continue;

                if (IsNativeComponentType(componentType))
                {
                    AddNativeComponent(ens, componentType);
                }
                else
                {
                    AddScriptMount(stableId, GetScriptTypeName(componentType));
                    sidecarChanged = true;
                }
            }

            if (sidecarChanged) SaveSidecar();
            EditorApplication.RequestRepaint();
        }
        catch (Exception ex)
        {
            status = "Component add failed: " + ex.Message;
        }
    }

    //验证依赖图并生成拓扑创建顺序。
    private void BuildComponentAddOrder(Type componentType, HashSet<Type> visiting, HashSet<Type> visited, List<Type> order)
    {
        if (!typeof(Component).IsAssignableFrom(componentType) || componentType.IsAbstract || !CanCreateComponent(componentType))
        {
            throw new InvalidOperationException($"Unsupported component type '{componentType.FullName}'.");
        }

        if (!visiting.Add(componentType))
        {
            throw new InvalidOperationException($"Circular component dependency at '{componentType.FullName}'.");
        }

        foreach (DependsOnComponentAttribute dependency in componentType.GetCustomAttributes<DependsOnComponentAttribute>(true))
        {
            foreach (Type requiredType in dependency.ComponentTypes)
            {
                if (requiredType == null) throw new InvalidOperationException($"Component '{componentType.FullName}' has a null dependency.");
                BuildComponentAddOrder(requiredType, visiting, visited, order);
            }
        }

        visiting.Remove(componentType);
        if (visited.Add(componentType)) order.Add(componentType);
    }

    //判断组件是否为唯一组件。
    private static bool IsUniqueComponent(Type type) => type.GetCustomAttribute<UniqueComponentAttribute>(true) != null;

    //判断类型能否由 Inspector 创建。
    private bool CanCreateComponent(Type type) => IsNativeComponentType(type) || scriptTypes.Contains(type);

    //判断类型是否对应原生绑定组件。
    private static bool IsNativeComponentType(Type type)
    {
        return type == typeof(StaticMeshRenderer) || type == typeof(RigidBody) ||
               type == typeof(BoxCollider) || type == typeof(SphereCollider) ||
               type == typeof(CapsuleCollider) || type == typeof(ConvexMeshCollider) ||
               type == typeof(TriangleMeshCollider) || type == typeof(CharacterController);
    }

    //判断 Ens 是否已拥有某个组件类型实例。
    private bool HasComponentInstance(Ens ens, string stableId, List<ScriptMount> mounts, Type type)
    {
        if (type == typeof(TransformComponent)) return ens.HasTransformComponent;
        if (type == typeof(StaticMeshRenderer)) return ens.HasStaticMeshRenderer;
        if (type == typeof(RigidBody)) return ens.HasRigidBody;
        if (type == typeof(BoxCollider)) return ens.GetComponent<BoxCollider>() != null;
        if (type == typeof(SphereCollider)) return ens.GetComponent<SphereCollider>() != null;
        if (type == typeof(CapsuleCollider)) return ens.GetComponent<CapsuleCollider>() != null;
        if (type == typeof(ConvexMeshCollider)) return ens.GetComponent<ConvexMeshCollider>() != null;
        if (type == typeof(TriangleMeshCollider)) return ens.GetComponent<TriangleMeshCollider>() != null;
        if (type == typeof(CharacterController)) return ens.HasCharacterController;
        return !string.IsNullOrEmpty(stableId) && mounts.Any(mount => TypeMatches(mount.Type, type));
    }

    //获取 stableId 对应的脚本挂载清单。
    private List<ScriptMount> GetScriptMounts(string stableId)
    {
        return sidecarScripts.TryGetValue(stableId, out List<ScriptMount>? mounts) ? mounts : [];
    }

    //创建一个原生绑定组件。
    private static void AddNativeComponent(Ens ens, Type type)
    {
        if (type == typeof(StaticMeshRenderer)) ens.AddStaticMeshRenderer();
        else if (type == typeof(RigidBody)) ens.AddRigidBody();
        else if (type == typeof(BoxCollider)) ens.AddBoxCollider();
        else if (type == typeof(SphereCollider)) ens.AddSphereCollider();
        else if (type == typeof(CapsuleCollider)) ens.AddCapsuleCollider();
        else if (type == typeof(ConvexMeshCollider)) ens.AddConvexMeshCollider();
        else if (type == typeof(TriangleMeshCollider)) ens.AddTriangleMeshCollider();
        else if (type == typeof(CharacterController)) ens.AddCharacterController();
    }

    //判断组件类型是否匹配搜索文本。
    private bool MatchesComponentSearch(string typeName, string search)
    {
        if (string.IsNullOrWhiteSpace(search)) return true;
        return typeName.Contains(search.Trim(), StringComparison.OrdinalIgnoreCase)
            || GetShortTypeName(typeName).Contains(search.Trim(), StringComparison.OrdinalIgnoreCase);
    }

    //新增一个 sidecar 脚本组件。
    private void AddScriptMount(string stableId, string typeName)
    {
        Type? type = FindScriptType(typeName);
        string scriptTypeName = type != null ? GetScriptTypeName(type) : StripAssemblyName(typeName);
        if (string.IsNullOrWhiteSpace(scriptTypeName)) return;

        if (!sidecarScripts.TryGetValue(stableId, out List<ScriptMount>? mounts))
        {
            mounts = [];
            sidecarScripts.Add(stableId, mounts);
        }

        if (type != null && IsUniqueComponent(type) && mounts.Any(mount => TypeMatches(mount.Type, type)))
        {
            return;
        }

        ScriptMount newMount = new() { Id = Guid.NewGuid().ToString("N"), StableId = stableId, Type = scriptTypeName };
        if (type != null)
        {
            foreach (FieldInfo field in GetSerializableFields(type))
            {
                GetSerializedValueForField(newMount, field);
            }
        }

        mounts.Add(newMount);
    }

    //绘制 sidecar 中一个脚本组件的字段。
    private void DrawSerializedScriptFields(ScriptMount mount)
    {
        Type? type = FindScriptType(mount.Type);
        if (type == null)
        {
            EditorGUI.Label("Build Game C# to edit fields.");
            return;
        }

        bool hasField = false;
        foreach (FieldInfo field in GetSerializableFields(type))
        {
            hasField = true;
            DrawSerializedField(mount, field);
        }

        if (!hasField)
        {
            EditorGUI.Label("No serialized fields.");
        }
    }

    //绘制一个 sidecar 字段。
    private void DrawSerializedField(ScriptMount mount, FieldInfo field)
    {
        ScriptSerializedValue serialized = GetSerializedValueForField(mount, field);
        string label = $"{field.Name}##serialized_{mount.StableId}_{mount.Id}_{field.Name}";
        if (!DrawSerializedValue(label, field.FieldType, serialized, out string newValue)) return;

        serialized.Type = GetValueTypeName(field.FieldType);
        serialized.Value = newValue;
        SaveSidecar();
        EditorApplication.RequestRepaint();
    }

    //读取或创建 sidecar 字段默认值。
    private ScriptSerializedValue GetSerializedValueForField(ScriptMount mount, FieldInfo field)
    {
        if (mount.Values.TryGetValue(field.Name, out ScriptSerializedValue? value))
        {
            if (string.IsNullOrEmpty(value.Type))
            {
                value.Type = GetValueTypeName(field.FieldType);
            }

            return value;
        }

        value = new ScriptSerializedValue
        {
            Type = GetValueTypeName(field.FieldType),
            Value = GetDefaultSerializedValue(field.FieldType),
        };
        mount.Values.Add(field.Name, value);
        return value;
    }

    //绘制可序列化字段值。
    private bool DrawSerializedValue(string label, Type type, ScriptSerializedValue serialized, out string newValue)
    {
        newValue = serialized.Value;
        if (type == typeof(bool))
        {
            bool typedValue = bool.TryParse(serialized.Value, out bool boolValue) && boolValue;
            if (!EditorGUI.Checkbox(label, ref typedValue)) return false;

            newValue = typedValue ? "true" : "false";
            return true;
        }

        if (type == typeof(int))
        {
            int typedValue = int.TryParse(serialized.Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int intValue) ? intValue : 0;
            if (!EditorGUI.InputInt(label, ref typedValue)) return false;

            newValue = typedValue.ToString(CultureInfo.InvariantCulture);
            return true;
        }

        if (type == typeof(float))
        {
            float typedValue = float.TryParse(serialized.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out float floatValue) ? floatValue : 0.0f;
            if (!EditorGUI.InputFloat(label, ref typedValue)) return false;

            newValue = typedValue.ToString(CultureInfo.InvariantCulture);
            return true;
        }

        if (type == typeof(string))
        {
            string typedValue = serialized.Value;
            if (!EditorGUI.InputText(label, ref typedValue)) return false;

            newValue = typedValue;
            return true;
        }

        if (type == typeof(vector3))
        {
            vector3 typedValue = ParseVector3(serialized.Value);
            if (!EditorGUI.InputVector3(label, ref typedValue)) return false;

            newValue = SerializeVector3(typedValue);
            return true;
        }

        if (typeof(Orbeden.Object).IsAssignableFrom(type))
        {
            Orbeden.Object? typedValue = EditorGUI.LoadObjectFieldAsset(type, serialized.Value);
            string resourceKey = serialized.Value;
            if (!EditorGUI.ObjectField(label, type, ref typedValue, ref resourceKey)) return false;

            newValue = resourceKey;
            return true;
        }

        EditorGUI.Label($"{label}: {type.Name}");
        return false;
    }

    //绘制与 sidecar 组件匹配的运行态脚本实例。
    private void DrawMatchingRuntimeScripts(IReadOnlyList<ScriptBehaviour> runtimeScripts)
    {
        foreach (ScriptBehaviour script in runtimeScripts)
        {
            Type type = script.GetType();
            EditorGUI.Label("Runtime Fields");
            DrawScriptMembers(script, type);
        }
    }

    //绘制没有 sidecar 挂载清单的运行态脚本实例。
    private void DrawUnmatchedRuntimeScripts(string stableId,
        IReadOnlyList<ScriptBehaviour> runtimeScripts,
        HashSet<ScriptBehaviour> drawnRuntimeScripts)
    {
        foreach (ScriptBehaviour script in runtimeScripts)
        {
            if (drawnRuntimeScripts.Contains(script)) continue;

            Type type = script.GetType();
            DrawCollapsibleComponentBlock($"{type.Name} (runtime)", $"runtime_script_{stableId}_{script.MountId}_{type.FullName}", false, () =>
            {
                ApplySerializedValuesToRuntimeScript(script, stableId);
                DrawScriptMembers(script, type);
            });
        }
    }

    //绘制一个 Inspector 组件块。
    private void DrawComponentBlock(string title, Action draw)
    {
        EditorGUI.BeginComponentBlock(title);
        try
        {
            draw();
        }
        finally
        {
            EditorGUI.EndComponentBlock();
        }
    }

    //绘制一个可折叠的 Inspector 组件块。
    private bool DrawCollapsibleComponentBlock(string title, string id, bool removable, Action draw)
    {
        bool expanded = EditorGUI.BeginCollapsibleComponentBlock(title, id, removable, out bool removeRequested);
        try
        {
            if (expanded && !removeRequested) draw();
        }
        finally
        {
            EditorGUI.EndComponentBlock();
        }

        return removeRequested;
    }

    //把 sidecar 字段值应用到运行态脚本实例一次。
    private void ApplySerializedValuesToRuntimeScript(ScriptBehaviour script, string stableId)
    {
        if (string.IsNullOrEmpty(stableId)) return;
        if (!runtimeSerializedApplied.Add(script)) return;

        Type type = script.GetType();
        ScriptMount? mount = FindScriptMount(stableId, script.MountId);
        if (mount == null) return;

        foreach (FieldInfo field in GetSerializableFields(type))
        {
            if (!mount.Values.TryGetValue(field.Name, out ScriptSerializedValue? serialized)) continue;
            if (!TryConvertSerializedValue(field.FieldType, serialized.Value, out object? value)) continue;

            field.SetValue(script, value);
        }
    }

    //绘制脚本字段和属性。
    private void DrawScriptMembers(object instance, Type type)
    {
        foreach (FieldInfo field in type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
        {
            if (!ShouldShowField(field)) continue;
            DrawValue(field.Name, field.FieldType, field.GetValue(instance), value => field.SetValue(instance, value));
        }

        foreach (PropertyInfo property in type.GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly))
        {
            if (!ShouldShowProperty(property)) continue;
            DrawValue(property.Name, property.PropertyType, property.GetValue(instance), value =>
            {
                if (property.SetMethod != null)
                {
                    property.SetValue(instance, value);
                }
            });
        }
    }

    //获取可序列化字段。
    private IEnumerable<FieldInfo> GetSerializableFields(Type type)
    {
        List<Type> chain = [];
        for (Type? current = type; current != null && current != typeof(ScriptBehaviour) && current != typeof(Component); current = current.BaseType)
        {
            chain.Add(current);
        }

        chain.Reverse();
        foreach (Type item in chain)
        {
            foreach (FieldInfo field in item.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (ShouldShowField(field))
                {
                    yield return field;
                }
            }
        }
    }

    //判断字段是否显示。
    private bool ShouldShowField(FieldInfo field)
    {
        if (field.IsStatic) return false;
        if (field.GetCustomAttribute<HideInInspectorAttribute>() != null) return false;
        return field.IsPublic || field.GetCustomAttribute<SerializeFieldAttribute>() != null;
    }

    //判断属性是否显示。
    private bool ShouldShowProperty(PropertyInfo property)
    {
        if (property.GetIndexParameters().Length != 0) return false;
        if (property.GetCustomAttribute<HideInInspectorAttribute>() != null) return false;
        return property.GetMethod != null && property.GetMethod.IsPublic;
    }

    //绘制基础值。
    private void DrawValue(string name, Type type, object? value, Action<object?> setValue)
    {
        //字段写入后请求下一帧刷新场景快照
        void WriteValue(object? updatedValue)
        {
            setValue(updatedValue);
            EditorApplication.RequestRepaint();
        }

        if (type == typeof(bool))
        {
            bool typedValue = value is bool boolValue && boolValue;
            if (EditorGUI.Checkbox(name, ref typedValue)) WriteValue(typedValue);
            return;
        }

        if (type == typeof(int))
        {
            int typedValue = value is int intValue ? intValue : 0;
            if (EditorGUI.InputInt(name, ref typedValue)) WriteValue(typedValue);
            return;
        }

        if (type == typeof(float))
        {
            float typedValue = value is float floatValue ? floatValue : 0.0f;
            if (EditorGUI.InputFloat(name, ref typedValue)) WriteValue(typedValue);
            return;
        }

        if (type == typeof(string))
        {
            string typedValue = value as string ?? string.Empty;
            if (EditorGUI.InputText(name, ref typedValue)) WriteValue(typedValue);
            return;
        }

        if (type == typeof(vector3))
        {
            vector3 typedValue = value is vector3 vectorValue ? vectorValue : new vector3();
            if (EditorGUI.InputVector3(name, ref typedValue)) WriteValue(typedValue);
            return;
        }

        if (typeof(Orbeden.Object).IsAssignableFrom(type))
        {
            Orbeden.Object? typedValue = value as Orbeden.Object;
            if (EditorGUI.ObjectField(name, type, ref typedValue)) WriteValue(typedValue);
            return;
        }

        EditorGUI.Label($"{name}: {value ?? type.Name}");
    }

    //查找 sidecar 脚本组件。
    private ScriptMount? FindScriptMount(string stableId, string mountId)
    {
        if (!sidecarScripts.TryGetValue(stableId, out List<ScriptMount>? mounts)) return null;
        return mounts.FirstOrDefault(mount => string.Equals(mount.Id, mountId, StringComparison.Ordinal));
    }

    //查找已反射到的脚本类型。
    private Type? FindScriptType(string typeName)
    {
        string scriptTypeName = StripAssemblyName(typeName);
        foreach (Type type in scriptTypes)
        {
            if (TypeMatches(scriptTypeName, type)) return type;
        }

        return gameAssembly?.GetType(scriptTypeName);
    }

    //判断 sidecar 类型名是否匹配反射类型。
    private bool TypeMatches(string typeName, Type type)
    {
        string scriptTypeName = StripAssemblyName(typeName);
        return string.Equals(scriptTypeName, GetScriptTypeName(type), StringComparison.Ordinal)
            || string.Equals(scriptTypeName, type.AssemblyQualifiedName, StringComparison.Ordinal);
    }

    //获取脚本类型全名。
    private string GetScriptTypeName(Type type)
    {
        return type.FullName ?? type.Name;
    }

    //获取脚本类型短名。
    private string GetShortTypeName(string typeName)
    {
        string scriptTypeName = StripAssemblyName(typeName);
        int index = scriptTypeName.LastIndexOf('.');
        return index >= 0 ? scriptTypeName[(index + 1)..] : scriptTypeName;
    }

    //去掉用户输入类型名中的程序集后缀。
    private string StripAssemblyName(string typeName)
    {
        string value = typeName.Trim();
        int commaIndex = value.IndexOf(',');
        return commaIndex >= 0 ? value[..commaIndex].Trim() : value;
    }

    //获取字段类型序列化名。
    private string GetValueTypeName(Type type)
    {
        if (type == typeof(bool)) return "bool";
        if (type == typeof(int)) return "int";
        if (type == typeof(float)) return "float";
        if (type == typeof(string)) return "string";
        if (type == typeof(vector3)) return "vector3";
        return type.FullName ?? type.Name;
    }

    //获取字段默认序列化值。
    private string GetDefaultSerializedValue(Type type)
    {
        if (type == typeof(bool)) return "false";
        if (type == typeof(int)) return "0";
        if (type == typeof(float)) return "0";
        if (type == typeof(string)) return string.Empty;
        if (type == typeof(vector3)) return "0 0 0";
        return string.Empty;
    }

    //把文本反序列化成字段值。
    private bool TryConvertSerializedValue(Type type, string text, out object? value)
    {
        value = null;
        if (type == typeof(bool))
        {
            if (!bool.TryParse(text, out bool boolValue)) return false;
            value = boolValue;
            return true;
        }

        if (type == typeof(int))
        {
            if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int intValue)) return false;
            value = intValue;
            return true;
        }

        if (type == typeof(float))
        {
            if (!float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out float floatValue)) return false;
            value = floatValue;
            return true;
        }

        if (type == typeof(string))
        {
            value = text;
            return true;
        }

        if (type == typeof(vector3))
        {
            value = ParseVector3(text);
            return true;
        }

        if (typeof(Orbeden.Object).IsAssignableFrom(type))
        {
            value = EditorGUI.LoadObjectFieldAsset(type, text);
            return string.IsNullOrEmpty(text) || value != null;
        }

        return false;
    }

    //判断 sidecar 类型名是否是资源对象。
    private bool IsSerializedResourceType(string typeName)
    {
        string value = StripAssemblyName(typeName);
        return value is "Orbeden.Mesh" or "Orbeden.Material" or "Orbeden.Shader";
    }

    //重映射一个 sidecar 资源 Key。
    private bool TryMapResourceKey(string value, string oldKey, string newKey, bool prefix, out string mapped)
    {
        mapped = value;
        int separator = value.IndexOf("//", StringComparison.Ordinal);
        string source = separator < 0 ? value : value[..separator];
        string subId = separator < 0 ? string.Empty : value[separator..];
        bool matches = string.Equals(source, oldKey, StringComparison.Ordinal);
        if (!matches && prefix && source.Length > oldKey.Length)
        {
            matches = source.StartsWith(oldKey, StringComparison.Ordinal) && source[oldKey.Length] == '/';
        }
        if (!matches) return false;

        mapped = string.IsNullOrEmpty(newKey) ? string.Empty : newKey + source[oldKey.Length..] + subId;
        return mapped != value;
    }

    //解析 vector3 文本。
    private vector3 ParseVector3(string text)
    {
        string[] parts = text.Split(new[] { ' ', ',' }, StringSplitOptions.RemoveEmptyEntries);
        float x = parts.Length > 0 && float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out float valueX) ? valueX : 0.0f;
        float y = parts.Length > 1 && float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out float valueY) ? valueY : 0.0f;
        float z = parts.Length > 2 && float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out float valueZ) ? valueZ : 0.0f;
        return new vector3(x, y, z);
    }

    //序列化 vector3 文本。
    private string SerializeVector3(vector3 value)
    {
        return string.Format(CultureInfo.InvariantCulture, "{0} {1} {2}", value.x, value.y, value.z);
    }

    //格式化 quaternion 文本。
    private string FormatQuaternion(quaternion value)
    {
        return string.Format(CultureInfo.InvariantCulture, "{0} {1} {2} {3}", value.x, value.y, value.z, value.w);
    }
}
