using System.Globalization;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
using System.Text.Json.Serialization;
using Orbeden;

namespace OrbedenEditor;

/// <summary>Editor CLR 中的用户游戏程序集域。</summary>
internal static class EditorGameDomain
{
    private sealed class GameAssemblyLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver resolver;

        /// <summary>创建可卸载的用户游戏程序集上下文。</summary>
        public GameAssemblyLoadContext(string assemblyPath) : base(isCollectible: true)
        {
            resolver = new AssemblyDependencyResolver(assemblyPath);
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
            return path != null ? LoadFromAssemblyPath(path) : null;
        }
    }

    private sealed class ScriptSidecarDocument
    {
        [JsonPropertyName("scripts")]
        public List<ScriptMount> Scripts { get; set; } = [];
    }

    private sealed class ScriptMount
    {
        [JsonPropertyName("stableId")]
        public string StableId { get; set; } = string.Empty;

        [JsonPropertyName("type")]
        public string Type { get; set; } = string.Empty;

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

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private static readonly List<Type> scriptTypes = [];
    private static readonly Dictionary<string, List<ScriptMount>> sidecarScripts = [];
    private static readonly Dictionary<string, string> componentSearches = [];
    private static readonly Dictionary<string, string> selectedAddTypes = [];
    private static readonly HashSet<ScriptBehaviour> runtimeSerializedApplied = [];
    private static GameAssemblyLoadContext? gameContext;
    private static Assembly? gameAssembly;
    private static string status = "Game assembly is not loaded.";
    private static string currentSidecarPath = string.Empty;

    /// <summary>加载用户游戏程序集和脚本挂载清单。</summary>
    public static void LoadGameAssembly(string assemblyPath, string sidecarPath)
    {
        UnloadReflectionAssembly();
        scriptTypes.Clear();
        sidecarScripts.Clear();
        componentSearches.Clear();
        selectedAddTypes.Clear();
        runtimeSerializedApplied.Clear();
        currentSidecarPath = sidecarPath;

        LoadSidecar(sidecarPath);
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
            gameAssembly = gameContext.LoadFromAssemblyPath(Path.GetFullPath(assemblyPath));
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
    public static void UnloadGameAssembly()
    {
        UnloadReflectionAssembly();
        scriptTypes.Clear();
        sidecarScripts.Clear();
        componentSearches.Clear();
        selectedAddTypes.Clear();
        runtimeSerializedApplied.Clear();
        currentSidecarPath = string.Empty;
        ScriptRuntimeRegistry.Clear();
        status = "Game assembly is not loaded.";
    }

    /// <summary>绘制选中 Ens 的 Inspector。</summary>
    public static void DrawInspector(EnsId selectedEns, string stableId)
    {
        bool visible = GUI.BeginPanel("Inspector");
        try
        {
            if (!visible) return;

            DrawInspectorContent(selectedEns, stableId);
        }
        finally
        {
            GUI.EndPanel();
        }
    }

    /// <summary>绘制选中 Ens 的 Inspector 内容。</summary>
    public static void DrawInspectorContent(EnsId selectedEns, string stableId)
    {
        if (selectedEns.IsNull)
        {
            GUI.Label("No Ens selected.");
            return;
        }

        Ens ens = Ens.FromId(selectedEns);
        if (!ens.IsValid)
        {
            GUI.Label("Selected Ens is not alive.");
            return;
        }

        DrawObjectHeader(ens, selectedEns, stableId);
        DrawInspectorStatus();
        DrawManagedComponents(ens, selectedEns, stableId);
    }

    //绘制选中对象摘要。
    private static void DrawObjectHeader(Ens ens, EnsId selectedEns, string stableId)
    {
        DrawComponentBlock("Selected Ens", () =>
        {
            string name = ens.Name;
            if (GUI.InputText("Name", ref name))
            {
                ens.Name = name;
            }

            GUI.Label($"Runtime Id: {selectedEns.id}:{selectedEns.version}");
            GUI.Label(string.IsNullOrEmpty(stableId) ? "Stable Id: <none>" : $"Stable Id: {stableId}");
        });
    }

    //绘制 Inspector 当前脚本程序集状态。
    private static void DrawInspectorStatus()
    {
        if (string.IsNullOrWhiteSpace(status)) return;

        GUI.Label($"C# Assembly: {status}");
    }

    //卸载仅用于 Inspector 反射的用户程序集上下文。
    private static void UnloadReflectionAssembly()
    {
        gameAssembly = null;
        if (gameContext == null) return;

        gameContext.Unload();
        gameContext = null;
    }

    //读取可加载类型，忽略坏类型。
    private static IEnumerable<Type> GetLoadableTypes(Assembly assembly)
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
    private static void LoadSidecar(string sidecarPath)
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
    private static ScriptMount? ReadScriptMount(JsonElement element)
    {
        string? stableId = element.TryGetProperty("stableId", out JsonElement stableIdElement) ? stableIdElement.GetString() : null;
        string? type = element.TryGetProperty("type", out JsonElement typeElement) ? typeElement.GetString() : null;
        if (string.IsNullOrWhiteSpace(stableId) || string.IsNullOrWhiteSpace(type)) return null;

        ScriptMount mount = new() { StableId = stableId, Type = StripAssemblyName(type) };
        if (!element.TryGetProperty("values", out JsonElement values)) return mount;
        if (values.ValueKind != JsonValueKind.Object) return mount;

        foreach (JsonProperty property in values.EnumerateObject())
        {
            mount.Values[property.Name] = ReadSerializedValue(property.Value);
        }

        return mount;
    }

    //读取一个序列化字段值。
    private static ScriptSerializedValue ReadSerializedValue(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object && element.TryGetProperty("value", out JsonElement valueElement))
        {
            string type = element.TryGetProperty("type", out JsonElement typeElement) ? typeElement.GetString() ?? string.Empty : string.Empty;
            return new ScriptSerializedValue { Type = type, Value = GetJsonValueText(valueElement) };
        }

        return new ScriptSerializedValue { Value = GetJsonValueText(element) };
    }

    //把 JsonElement 转成可编辑文本。
    private static string GetJsonValueText(JsonElement element)
    {
        return element.ValueKind == JsonValueKind.String ? element.GetString() ?? string.Empty : element.GetRawText();
    }

    //保存 world sidecar 脚本挂载清单。
    private static void SaveSidecar()
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
    private static void DrawBoundComponents(Ens ens, EnsId selectedEns)
    {
        if (ens.HasSpaceComponent)
        {
            DrawCollapsibleComponentBlock("SpaceComponent", $"bound_{selectedEns.id}_SpaceComponent", false, () =>
            {
                SpaceComponent space = ens.Space;
                vector3 localPosition = space.localPosition;
                if (GUI.InputVector3("localPosition", ref localPosition))
                {
                    space.localPosition = localPosition;
                }

                vector3 localScale = space.localScale;
                if (GUI.InputVector3("localScale", ref localScale))
                {
                    space.localScale = localScale;
                }

                quaternion localRotation = space.localRotation;
                GUI.Label($"localRotation: {FormatQuaternion(localRotation)}");
                GUI.Label($"worldPosition: {SerializeVector3(space.worldPosition)}");
                GUI.Label($"worldRotation: {FormatQuaternion(space.worldRotation)}");
            });
        }

        if (ens.HasStaticMeshRenderer)
        {
            StaticMeshRenderer? renderer = ens.GetComponent<StaticMeshRenderer>();
            if (renderer != null && DrawCollapsibleComponentBlock("StaticMeshRenderer", $"bound_{selectedEns.id}_StaticMeshRenderer", true, () =>
            {
                bool enabled = renderer.enabled;
                if (GUI.Checkbox("enabled", ref enabled))
                {
                    renderer.enabled = enabled;
                }

                Mesh? mesh = renderer.mesh;
                if (mesh != null && mesh.IsValid)
                {
                    GUI.Label($"mesh: {mesh.InstanceId}");
                    GUI.Label($"mesh stats: {mesh.vertexCount} vertices, {mesh.indexCount} indices, {mesh.subMeshCount} subMeshes");
                }
                else
                {
                    GUI.Label("mesh: (none)");
                }

                bool castShadows = renderer.castShadows;
                if (GUI.Checkbox("castShadows", ref castShadows))
                {
                    renderer.castShadows = castShadows;
                }

                bool receiveShadows = renderer.receiveShadows;
                if (GUI.Checkbox("receiveShadows", ref receiveShadows))
                {
                    renderer.receiveShadows = receiveShadows;
                }
            }))
            {
                Orbeden.Object.Destroy(renderer);
            }
        }

        DrawBoundComponent(ens.GetComponent<RigidBody>(), selectedEns, component => DrawScriptMembers(component, typeof(RigidBody)));
        DrawBoundComponent(ens.GetComponent<Collider>(), selectedEns, component => DrawScriptMembers(component, typeof(Collider)));
        DrawBoundComponent(ens.GetComponent<CharacterController>(), selectedEns, component => DrawScriptMembers(component, typeof(CharacterController)));
    }

    //绘制一个可移除的引擎 Bind 组件。
    private static void DrawBoundComponent<T>(T? component, EnsId selectedEns, Action<T> draw) where T : Component
    {
        if (component == null) return;

        string typeName = typeof(T).Name;
        if (DrawCollapsibleComponentBlock(typeName, $"bound_{selectedEns.id}_{typeName}", true, () => draw(component)))
        {
            Orbeden.Object.Destroy(component);
        }
    }

    //统计当前对象上的引擎 Bind 组件数量。
    private static int GetBoundComponentCount(Ens ens)
    {
        int count = 0;
        if (ens.HasSpaceComponent) count++;
        if (ens.HasStaticMeshRenderer) count++;
        if (ens.HasRigidBody) count++;
        if (ens.HasCollider) count++;
        if (ens.HasCharacterController) count++;
        return count;
    }

    //绘制引擎 Bind 和用户脚本组成的 C# 组件列表。
    private static void DrawManagedComponents(Ens ens, EnsId selectedEns, string stableId)
    {
        List<ScriptMount>? mounts = null;
        if (!string.IsNullOrEmpty(stableId)) sidecarScripts.TryGetValue(stableId, out mounts);
        mounts ??= [];
        IReadOnlyList<ScriptBehaviour> runtimeScripts = ScriptRuntimeRegistry.GetScripts(selectedEns);
        HashSet<ScriptBehaviour> drawnRuntimeScripts = [];

        GUI.Label($"C# Components ({GetBoundComponentCount(ens) + mounts.Count})");
        DrawBoundComponents(ens, selectedEns);
        if (string.IsNullOrEmpty(stableId))
        {
            GUI.Label("Selected Ens has no stableId. C# script components require a stableId.");
            DrawComponentBlock("Add C# Component", () => DrawAddComponentControls(ens, stableId, mounts));
            return;
        }

        int removeIndex = -1;
        for (int index = 0; index < mounts.Count; index++)
        {
            ScriptMount mount = mounts[index];
            string title = GetShortTypeName(mount.Type);
            List<ScriptBehaviour> matchingRuntimeScripts = runtimeScripts
                .Where(script => TypeMatches(mount.Type, script.GetType()))
                .ToList();
            foreach (ScriptBehaviour script in matchingRuntimeScripts)
            {
                drawnRuntimeScripts.Add(script);
                ApplySerializedValuesToRuntimeScript(script, stableId);
            }

            bool removeRequested = DrawCollapsibleComponentBlock(title, $"script_{stableId}_{mount.Type}", true, () =>
            {
                GUI.Label($"Type: {mount.Type}");
                GUI.Label("Serialized Fields");
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
            return;
        }

        DrawUnmatchedRuntimeScripts(stableId, runtimeScripts, drawnRuntimeScripts);
        DrawComponentBlock("Add C# Component", () => DrawAddComponentControls(ens, stableId, mounts));
    }

    //绘制新增 C# 组件控件。
    private static void DrawAddComponentControls(Ens ens, string stableId, List<ScriptMount> mounts)
    {
        List<Type> availableTypes = GetAvailableComponentTypes(ens, stableId, mounts);
        if (availableTypes.Count == 0)
        {
            GUI.Label("No C# component types are available.");
            return;
        }

        string search = componentSearches.TryGetValue(stableId, out string? searchValue) ? searchValue : string.Empty;
        string selectedTypeName = selectedAddTypes.TryGetValue(stableId, out string? selectedValue) ? selectedValue : string.Empty;
        if (!availableTypes.Any(type => string.Equals(GetScriptTypeName(type), selectedTypeName, StringComparison.Ordinal)))
        {
            selectedTypeName = string.Empty;
            selectedAddTypes.Remove(stableId);
        }

        string preview = string.IsNullOrEmpty(selectedTypeName) ? "Select a C# component" : GetShortTypeName(selectedTypeName);
        if (GUI.BeginCombo($"Component##add_component_combo_{stableId}", preview))
        {
            try
            {
                if (GUI.InputText($"Search##add_component_search_{stableId}", ref search))
                {
                    componentSearches[stableId] = search;
                }

                bool hasMatch = false;
                foreach (Type type in availableTypes)
                {
                    string typeName = GetScriptTypeName(type);
                    if (!MatchesComponentSearch(typeName, search)) continue;

                    hasMatch = true;
                    bool selected = string.Equals(typeName, selectedTypeName, StringComparison.Ordinal);
                    if (GUI.Selectable($"{GetShortTypeName(typeName)}##add_component_{stableId}_{typeName}", selected))
                    {
                        selectedTypeName = typeName;
                        selectedAddTypes[stableId] = typeName;
                    }
                }

                if (!hasMatch) GUI.Label("No matching C# components.");
            }
            finally
            {
                GUI.EndCombo();
            }
        }

        if (GUI.Button($"Add Component##add_component_button_{stableId}") && !string.IsNullOrEmpty(selectedTypeName))
        {
            Type? selectedType = availableTypes.FirstOrDefault(type => string.Equals(GetScriptTypeName(type), selectedTypeName, StringComparison.Ordinal));
            if (selectedType != null) AddComponent(ens, stableId, selectedType);
            componentSearches[stableId] = string.Empty;
            selectedAddTypes.Remove(stableId);
        }
    }

    //获取当前可添加的引擎 Bind 与游戏脚本组件类型。
    private static List<Type> GetAvailableComponentTypes(Ens ens, string stableId, List<ScriptMount> mounts)
    {
        List<Type> types = [];
        if (!ens.HasStaticMeshRenderer) types.Add(typeof(StaticMeshRenderer));
        if (!ens.HasRigidBody) types.Add(typeof(RigidBody));
        if (!ens.HasCollider) types.Add(typeof(Collider));
        if (!ens.HasCharacterController) types.Add(typeof(CharacterController));
        if (!string.IsNullOrEmpty(stableId))
        {
            types.AddRange(scriptTypes.Where(type => !mounts.Any(mount => TypeMatches(mount.Type, type))));
        }

        types.Sort((left, right) => string.Compare(GetScriptTypeName(left), GetScriptTypeName(right), StringComparison.Ordinal));
        return types;
    }

    //按托管类型添加引擎 Bind 或游戏脚本组件。
    private static void AddComponent(Ens ens, string stableId, Type type)
    {
        if (type == typeof(StaticMeshRenderer)) ens.AddStaticMeshRenderer();
        else if (type == typeof(RigidBody)) ens.AddRigidBody();
        else if (type == typeof(Collider)) ens.AddCollider();
        else if (type == typeof(CharacterController)) ens.AddCharacterController();
        else AddScriptMount(stableId, GetScriptTypeName(type));
    }

    //判断组件类型是否匹配搜索文本。
    private static bool MatchesComponentSearch(string typeName, string search)
    {
        if (string.IsNullOrWhiteSpace(search)) return true;
        return typeName.Contains(search.Trim(), StringComparison.OrdinalIgnoreCase)
            || GetShortTypeName(typeName).Contains(search.Trim(), StringComparison.OrdinalIgnoreCase);
    }

    //新增一个 sidecar 脚本组件。
    private static void AddScriptMount(string stableId, string typeName)
    {
        Type? type = FindScriptType(typeName);
        string scriptTypeName = type != null ? GetScriptTypeName(type) : StripAssemblyName(typeName);
        if (string.IsNullOrWhiteSpace(scriptTypeName)) return;

        if (!sidecarScripts.TryGetValue(stableId, out List<ScriptMount>? mounts))
        {
            mounts = [];
            sidecarScripts.Add(stableId, mounts);
        }

        if (mounts.Any(mount => string.Equals(mount.Type, scriptTypeName, StringComparison.Ordinal)))
        {
            return;
        }

        ScriptMount newMount = new() { StableId = stableId, Type = scriptTypeName };
        if (type != null)
        {
            foreach (FieldInfo field in GetSerializableFields(type))
            {
                GetSerializedValueForField(newMount, field);
            }
        }

        mounts.Add(newMount);
        SaveSidecar();
    }

    //绘制 sidecar 中一个脚本组件的字段。
    private static void DrawSerializedScriptFields(ScriptMount mount)
    {
        Type? type = FindScriptType(mount.Type);
        if (type == null)
        {
            GUI.Label("Build C# to edit fields.");
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
            GUI.Label("No serialized fields.");
        }
    }

    //绘制一个 sidecar 字段。
    private static void DrawSerializedField(ScriptMount mount, FieldInfo field)
    {
        ScriptSerializedValue serialized = GetSerializedValueForField(mount, field);
        string label = $"{field.Name}##serialized_{mount.StableId}_{mount.Type}_{field.Name}";
        if (!DrawSerializedValue(label, field.FieldType, serialized, out string newValue)) return;

        serialized.Type = GetValueTypeName(field.FieldType);
        serialized.Value = newValue;
        SaveSidecar();
    }

    //读取或创建 sidecar 字段默认值。
    private static ScriptSerializedValue GetSerializedValueForField(ScriptMount mount, FieldInfo field)
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
    private static bool DrawSerializedValue(string label, Type type, ScriptSerializedValue serialized, out string newValue)
    {
        newValue = serialized.Value;
        if (type == typeof(bool))
        {
            bool typedValue = bool.TryParse(serialized.Value, out bool boolValue) && boolValue;
            if (!GUI.Checkbox(label, ref typedValue)) return false;

            newValue = typedValue ? "true" : "false";
            return true;
        }

        if (type == typeof(int))
        {
            int typedValue = int.TryParse(serialized.Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int intValue) ? intValue : 0;
            if (!GUI.InputInt(label, ref typedValue)) return false;

            newValue = typedValue.ToString(CultureInfo.InvariantCulture);
            return true;
        }

        if (type == typeof(float))
        {
            float typedValue = float.TryParse(serialized.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out float floatValue) ? floatValue : 0.0f;
            if (!GUI.InputFloat(label, ref typedValue)) return false;

            newValue = typedValue.ToString(CultureInfo.InvariantCulture);
            return true;
        }

        if (type == typeof(string))
        {
            string typedValue = serialized.Value;
            if (!GUI.InputText(label, ref typedValue)) return false;

            newValue = typedValue;
            return true;
        }

        if (type == typeof(vector3))
        {
            vector3 typedValue = ParseVector3(serialized.Value);
            if (!GUI.InputVector3(label, ref typedValue)) return false;

            newValue = SerializeVector3(typedValue);
            return true;
        }

        GUI.Label($"{label}: {type.Name}");
        return false;
    }

    //绘制与 sidecar 组件匹配的运行态脚本实例。
    private static void DrawMatchingRuntimeScripts(IReadOnlyList<ScriptBehaviour> runtimeScripts)
    {
        foreach (ScriptBehaviour script in runtimeScripts)
        {
            Type type = script.GetType();
            GUI.Label("Runtime Fields");
            DrawScriptMembers(script, type);
        }
    }

    //绘制没有 sidecar 挂载清单的运行态脚本实例。
    private static void DrawUnmatchedRuntimeScripts(string stableId,
        IReadOnlyList<ScriptBehaviour> runtimeScripts,
        HashSet<ScriptBehaviour> drawnRuntimeScripts)
    {
        foreach (ScriptBehaviour script in runtimeScripts)
        {
            if (drawnRuntimeScripts.Contains(script)) continue;

            Type type = script.GetType();
            DrawCollapsibleComponentBlock($"{type.Name} (runtime)", $"runtime_script_{stableId}_{type.FullName}", false, () =>
            {
                ApplySerializedValuesToRuntimeScript(script, stableId);
                DrawScriptMembers(script, type);
            });
        }
    }

    //绘制一个 Inspector 组件块。
    private static void DrawComponentBlock(string title, Action draw)
    {
        GUI.BeginComponentBlock(title);
        try
        {
            draw();
        }
        finally
        {
            GUI.EndComponentBlock();
        }
    }

    //绘制一个可折叠的 Inspector 组件块。
    private static bool DrawCollapsibleComponentBlock(string title, string id, bool removable, Action draw)
    {
        bool expanded = GUI.BeginCollapsibleComponentBlock(title, id, removable, out bool removeRequested);
        try
        {
            if (expanded && !removeRequested) draw();
        }
        finally
        {
            GUI.EndComponentBlock();
        }

        return removeRequested;
    }

    //把 sidecar 字段值应用到运行态脚本实例一次。
    private static void ApplySerializedValuesToRuntimeScript(ScriptBehaviour script, string stableId)
    {
        if (string.IsNullOrEmpty(stableId)) return;
        if (!runtimeSerializedApplied.Add(script)) return;

        Type type = script.GetType();
        ScriptMount? mount = FindScriptMount(stableId, type);
        if (mount == null) return;

        foreach (FieldInfo field in GetSerializableFields(type))
        {
            if (!mount.Values.TryGetValue(field.Name, out ScriptSerializedValue? serialized)) continue;
            if (!TryConvertSerializedValue(field.FieldType, serialized.Value, out object? value)) continue;

            field.SetValue(script, value);
        }
    }

    //绘制脚本字段和属性。
    private static void DrawScriptMembers(object instance, Type type)
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
    private static IEnumerable<FieldInfo> GetSerializableFields(Type type)
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
    private static bool ShouldShowField(FieldInfo field)
    {
        if (field.IsStatic) return false;
        if (field.GetCustomAttribute<HideInInspectorAttribute>() != null) return false;
        return field.IsPublic || field.GetCustomAttribute<SerializeFieldAttribute>() != null;
    }

    //判断属性是否显示。
    private static bool ShouldShowProperty(PropertyInfo property)
    {
        if (property.GetIndexParameters().Length != 0) return false;
        if (property.GetCustomAttribute<HideInInspectorAttribute>() != null) return false;
        return property.GetMethod != null && property.GetMethod.IsPublic;
    }

    //绘制基础值。
    private static void DrawValue(string name, Type type, object? value, Action<object?> setValue)
    {
        if (type == typeof(bool))
        {
            bool typedValue = value is bool boolValue && boolValue;
            if (GUI.Checkbox(name, ref typedValue)) setValue(typedValue);
            return;
        }

        if (type == typeof(int))
        {
            int typedValue = value is int intValue ? intValue : 0;
            if (GUI.InputInt(name, ref typedValue)) setValue(typedValue);
            return;
        }

        if (type == typeof(float))
        {
            float typedValue = value is float floatValue ? floatValue : 0.0f;
            if (GUI.InputFloat(name, ref typedValue)) setValue(typedValue);
            return;
        }

        if (type == typeof(string))
        {
            string typedValue = value as string ?? string.Empty;
            if (GUI.InputText(name, ref typedValue)) setValue(typedValue);
            return;
        }

        if (type == typeof(vector3))
        {
            vector3 typedValue = value is vector3 vectorValue ? vectorValue : new vector3();
            if (GUI.InputVector3(name, ref typedValue)) setValue(typedValue);
            return;
        }

        GUI.Label($"{name}: {value ?? type.Name}");
    }

    //查找 sidecar 脚本组件。
    private static ScriptMount? FindScriptMount(string stableId, Type type)
    {
        if (!sidecarScripts.TryGetValue(stableId, out List<ScriptMount>? mounts)) return null;
        return mounts.FirstOrDefault(mount => TypeMatches(mount.Type, type));
    }

    //查找已反射到的脚本类型。
    private static Type? FindScriptType(string typeName)
    {
        string scriptTypeName = StripAssemblyName(typeName);
        foreach (Type type in scriptTypes)
        {
            if (TypeMatches(scriptTypeName, type)) return type;
        }

        return gameAssembly?.GetType(scriptTypeName);
    }

    //判断 sidecar 类型名是否匹配反射类型。
    private static bool TypeMatches(string typeName, Type type)
    {
        string scriptTypeName = StripAssemblyName(typeName);
        return string.Equals(scriptTypeName, GetScriptTypeName(type), StringComparison.Ordinal)
            || string.Equals(scriptTypeName, type.AssemblyQualifiedName, StringComparison.Ordinal);
    }

    //获取脚本类型全名。
    private static string GetScriptTypeName(Type type)
    {
        return type.FullName ?? type.Name;
    }

    //获取脚本类型短名。
    private static string GetShortTypeName(string typeName)
    {
        string scriptTypeName = StripAssemblyName(typeName);
        int index = scriptTypeName.LastIndexOf('.');
        return index >= 0 ? scriptTypeName[(index + 1)..] : scriptTypeName;
    }

    //去掉用户输入类型名中的程序集后缀。
    private static string StripAssemblyName(string typeName)
    {
        string value = typeName.Trim();
        int commaIndex = value.IndexOf(',');
        return commaIndex >= 0 ? value[..commaIndex].Trim() : value;
    }

    //获取字段类型序列化名。
    private static string GetValueTypeName(Type type)
    {
        if (type == typeof(bool)) return "bool";
        if (type == typeof(int)) return "int";
        if (type == typeof(float)) return "float";
        if (type == typeof(string)) return "string";
        if (type == typeof(vector3)) return "vector3";
        return type.FullName ?? type.Name;
    }

    //获取字段默认序列化值。
    private static string GetDefaultSerializedValue(Type type)
    {
        if (type == typeof(bool)) return "false";
        if (type == typeof(int)) return "0";
        if (type == typeof(float)) return "0";
        if (type == typeof(string)) return string.Empty;
        if (type == typeof(vector3)) return "0 0 0";
        return string.Empty;
    }

    //把文本反序列化成字段值。
    private static bool TryConvertSerializedValue(Type type, string text, out object? value)
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

        return false;
    }

    //解析 vector3 文本。
    private static vector3 ParseVector3(string text)
    {
        string[] parts = text.Split(new[] { ' ', ',' }, StringSplitOptions.RemoveEmptyEntries);
        float x = parts.Length > 0 && float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out float valueX) ? valueX : 0.0f;
        float y = parts.Length > 1 && float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out float valueY) ? valueY : 0.0f;
        float z = parts.Length > 2 && float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out float valueZ) ? valueZ : 0.0f;
        return new vector3(x, y, z);
    }

    //序列化 vector3 文本。
    private static string SerializeVector3(vector3 value)
    {
        return string.Format(CultureInfo.InvariantCulture, "{0} {1} {2}", value.x, value.y, value.z);
    }

    //格式化 quaternion 文本。
    private static string FormatQuaternion(quaternion value)
    {
        return string.Format(CultureInfo.InvariantCulture, "{0} {1} {2} {3}", value.x, value.y, value.z, value.w);
    }
}
