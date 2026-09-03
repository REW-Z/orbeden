using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;

namespace Orbeden;

/// <summary>管理当前 World 的托管脚本生命周期和预绑定回调表。</summary>
internal static class ScriptRuntime
{
    private const DynamicallyAccessedMemberTypes ScriptMemberTypes =
        DynamicallyAccessedMemberTypes.PublicConstructors |
        DynamicallyAccessedMemberTypes.PublicMethods |
        DynamicallyAccessedMemberTypes.NonPublicMethods;

    private sealed class GameAssemblyLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver resolver;

        /// <summary>创建可卸载的游戏程序集上下文。</summary>
        public GameAssemblyLoadContext(string assemblyPath) : base(isCollectible: true)
        {
            resolver = new AssemblyDependencyResolver(assemblyPath);
        }

        /// <summary>从内存加载程序集，避免锁定构建输出。</summary>
        [UnconditionalSuppressMessage("Trimming", "IL2026", Justification = "This path is used only by the non-trimmed Editor CLR runtime.")]
        public Assembly LoadAssemblyFile(string assemblyPath)
        {
            using FileStream assemblyStream = File.OpenRead(assemblyPath);
            string symbolPath = Path.ChangeExtension(assemblyPath, ".pdb");
            if (!File.Exists(symbolPath)) return LoadFromStream(assemblyStream);

            using FileStream symbolStream = File.OpenRead(symbolPath);
            return LoadFromStream(assemblyStream, symbolStream);
        }

        /// <summary>复用引擎托管程序集并解析游戏依赖。</summary>
        protected override Assembly? Load(AssemblyName assemblyName)
        {
            Assembly runtimeAssembly = typeof(ScriptBehaviour).Assembly;
            if (assemblyName.Name == runtimeAssembly.GetName().Name) return runtimeAssembly;

            string? path = resolver.ResolveAssemblyToPath(assemblyName);
            return path != null ? LoadAssemblyFile(path) : null;
        }
    }

    private sealed class ScriptMount
    {
        public string Id = string.Empty;
        public string StableId = string.Empty;
        public string Type = string.Empty;
        public bool Enabled = true;
        public Dictionary<string, string> Values = [];
    }

    private sealed class ScriptTypeFactory
    {
        public ConstructorInfo Constructor = null!;
        public MethodInfo? SerializedValueApplier;
        public MethodInfo? Start;
        public MethodInfo? Update;
        public MethodInfo? FixedUpdate;
        public MethodInfo? LateUpdate;
        public MethodInfo? DrawGUI;
        public MethodInfo? End;
    }

    private sealed class ScriptInstance
    {
        public ScriptBehaviour Script = null!;
        public Action? Start;
        public Action<float>? Update;
        public Action<float>? FixedUpdate;
        public Action<float>? LateUpdate;
        public Action? DrawGUI;
        public Action? End;
        public bool WorldActive;
        public bool Started;
        public bool Destroyed;
    }

    private readonly record struct UpdateInvocation(ScriptInstance Instance, Action<float> Callback);
    private readonly record struct Invocation(ScriptInstance Instance, Action Callback);

    private static readonly List<ScriptInstance> scripts = [];
    private static readonly Dictionary<ScriptBehaviour, ScriptInstance> scriptByObject = [];
    private static readonly Dictionary<EnsId, List<ScriptInstance>> scriptsByEns = [];
    private static readonly Dictionary<string, ScriptTypeFactory> scriptFactories = [];
    private static readonly List<UpdateInvocation> updateInvocations = [];
    private static readonly List<UpdateInvocation> fixedUpdateInvocations = [];
    private static readonly List<UpdateInvocation> lateUpdateInvocations = [];
    private static readonly List<Invocation> drawGuiInvocations = [];
    private static GameAssemblyLoadContext? gameContext;
    private static Assembly? gameAssembly;
    private static bool invocationListsDirty;
    private static bool hasDestroyedScripts;
    private static int dispatchDepth;

    /// <summary>初始化原生绑定并创建当前 World 的 C# 脚本。</summary>
    public static void Initialize(IntPtr nativeApi)
    {
        OrbedenCoreRuntime.Initialize(nativeApi);
        ManagedScriptInterop.Shutdown();
        ShutdownMountedScripts();
        ScriptRuntimeRegistry.Clear();
        ManagedScriptInterop.Initialize();

        string sidecarPath = GetStartupWorldSidecarPath();
        if (string.IsNullOrEmpty(sidecarPath) || !File.Exists(sidecarPath))
        {
            Console.WriteLine("ScriptRuntime: world script sidecar was not found.");
            return;
        }

        foreach (ScriptMount mount in ReadScriptMounts(sidecarPath))
        {
            Ens ens = Ens.Find(mount.StableId);
            if (!ens.IsValid)
            {
                Console.WriteLine($"ScriptRuntime: missing Ens stableId '{mount.StableId}'.");
                continue;
            }

            ScriptInstance? instance = CreateScript(mount, ens);
            if (instance == null)
            {
                Console.WriteLine($"ScriptRuntime: unsupported script type '{mount.Type}'.");
                continue;
            }

            RegisterScript(instance);
        }

        invocationListsDirty = true;
        RebuildInvocationLists();
    }

    /// <summary>执行预绑定的 Update delegate 表。</summary>
    public static void Update(float deltaTime)
    {
        PreparePhase();
        dispatchDepth++;
        try
        {
            foreach (UpdateInvocation invocation in updateInvocations)
            {
                Invoke(invocation.Instance, invocation.Callback, deltaTime, "OnUpdate");
            }
        }
        finally
        {
            CompletePhase();
        }
    }

    /// <summary>执行预绑定的 FixedUpdate delegate 表。</summary>
    public static void FixedUpdate(float fixedDeltaTime)
    {
        PreparePhase();
        dispatchDepth++;
        try
        {
            foreach (UpdateInvocation invocation in fixedUpdateInvocations)
            {
                Invoke(invocation.Instance, invocation.Callback, fixedDeltaTime, "OnFixedUpdate");
            }
        }
        finally
        {
            CompletePhase();
        }
    }

    /// <summary>执行预绑定的 LateUpdate delegate 表。</summary>
    public static void LateUpdate(float deltaTime)
    {
        PreparePhase();
        dispatchDepth++;
        try
        {
            foreach (UpdateInvocation invocation in lateUpdateInvocations)
            {
                Invoke(invocation.Instance, invocation.Callback, deltaTime, "OnLateUpdate");
            }
        }
        finally
        {
            CompletePhase();
        }
    }

    /// <summary>执行预绑定的 DrawGUI delegate 表。</summary>
    public static void DrawGUI()
    {
        PreparePhase();
        dispatchDepth++;
        try
        {
            foreach (Invocation invocation in drawGuiInvocations)
            {
                Invoke(invocation.Instance, invocation.Callback, "OnDrawGUI");
            }
        }
        finally
        {
            CompletePhase();
        }
    }

    /// <summary>在运行态挂载一个脚本，并在下一个脚本阶段参与 Start 和阶段表。</summary>
    public static ScriptBehaviour? AddMountedScript(EnsId ensId,
        string mountId,
        string typeName,
        bool enabled,
        IReadOnlyDictionary<string, string>? serializedValues = null)
    {
        if (ensId.IsNull || string.IsNullOrWhiteSpace(mountId) || string.IsNullOrWhiteSpace(typeName)) return null;
        Ens ens = Ens.FromId(ensId);
        if (!ens.IsValid) return null;
        if (scriptsByEns.TryGetValue(ensId, out List<ScriptInstance>? existing)
            && existing.Any(instance => string.Equals(instance.Script.MountId, mountId, StringComparison.Ordinal)))
        {
            return null;
        }

        ScriptMount mount = new()
        {
            Id = mountId,
            Type = typeName,
            Enabled = enabled,
            Values = serializedValues == null
                ? []
                : new Dictionary<string, string>(serializedValues, StringComparer.Ordinal),
        };
        ScriptInstance? instance = CreateScript(mount, ens);
        if (instance == null) return null;
        RegisterScript(instance);
        invocationListsDirty = true;
        return instance.Script;
    }

    /// <summary>从运行态移除指定挂载脚本；已 Start 的实例只执行一次 End。</summary>
    public static bool RemoveMountedScript(EnsId ensId, string mountId)
    {
        if (!scriptsByEns.TryGetValue(ensId, out List<ScriptInstance>? instances)) return false;
        ScriptInstance? instance = instances.FirstOrDefault(candidate =>
            !candidate.Destroyed && string.Equals(candidate.Script.MountId, mountId, StringComparison.Ordinal));
        if (instance == null) return false;
        DestroyScript(instance);
        invocationListsDirty = true;
        if (dispatchDepth == 0) RemoveDestroyedScripts();
        return true;
    }

    /// <summary>响应 ScriptBehaviour.enabled 变化。</summary>
    internal static void OnEnabledChanged(ScriptBehaviour script)
    {
        if (scriptByObject.ContainsKey(script)) invocationListsDirty = true;
    }

    /// <summary>响应原生 World 转发的 Ens 活动状态变化。</summary>
    public static void OnEnsWorldActiveChanged(EnsId ens, bool worldActive)
    {
        if (!scriptsByEns.TryGetValue(ens, out List<ScriptInstance>? instances)) return;

        foreach (ScriptInstance instance in instances)
        {
            instance.WorldActive = worldActive;
        }
        invocationListsDirty = true;
    }

    /// <summary>响应原生 World 转发的 Ens 销毁事件。</summary>
    public static void OnEnsDestroyed(EnsId ens)
    {
        if (!scriptsByEns.TryGetValue(ens, out List<ScriptInstance>? instances)) return;

        foreach (ScriptInstance instance in instances.ToArray())
        {
            DestroyScript(instance);
        }
        invocationListsDirty = true;
        if (dispatchDepth == 0) RemoveDestroyedScripts();
    }

    /// <summary>关闭当前 World 的脚本实例和程序集引用。</summary>
    public static void Shutdown()
    {
        ShutdownMountedScripts();
        ScriptRuntimeRegistry.Clear();
        ManagedScriptInterop.Shutdown();
        scriptFactories.Clear();
        ManagedTypeMetadataCache.Clear();
        gameAssembly = null;
        if (gameContext != null)
        {
            gameContext.Unload();
            gameContext = null;
        }
    }

    /// <summary>装载 Editor CLR 模式下的用户游戏程序集。</summary>
    internal static bool LoadGameAssembly(string assemblyPath)
    {
        if (scripts.Count != 0 || string.IsNullOrWhiteSpace(assemblyPath) || !File.Exists(assemblyPath)) return false;

        try
        {
            gameAssembly = null;
            if (gameContext != null)
            {
                gameContext.Unload();
                gameContext = null;
            }

            string fullPath = Path.GetFullPath(assemblyPath);
            gameContext = new GameAssemblyLoadContext(fullPath);
            gameAssembly = gameContext.LoadAssemblyFile(fullPath);
            scriptFactories.Clear();
            ManagedTypeMetadataCache.Clear();
            return true;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"ScriptRuntime: game assembly load failed. {exception}");
            gameAssembly = null;
            if (gameContext != null)
            {
                gameContext.Unload();
                gameContext = null;
            }
            return false;
        }
    }

    //注册脚本实例及其 Ens 索引
    private static void RegisterScript(ScriptInstance instance)
    {
        scripts.Add(instance);
        scriptByObject.Add(instance.Script, instance);
        if (!scriptsByEns.TryGetValue(instance.Script.EnsId, out List<ScriptInstance>? instances))
        {
            instances = [];
            scriptsByEns.Add(instance.Script.EnsId, instances);
        }
        instances.Add(instance);
    }

    //结束并清空当前 World 的脚本实例
    private static void ShutdownMountedScripts()
    {
        for (int index = scripts.Count - 1; index >= 0; index--)
        {
            DestroyScript(scripts[index]);
        }

        scripts.Clear();
        scriptByObject.Clear();
        scriptsByEns.Clear();
        updateInvocations.Clear();
        fixedUpdateInvocations.Clear();
        lateUpdateInvocations.Clear();
        drawGuiInvocations.Clear();
        invocationListsDirty = false;
        hasDestroyedScripts = false;
        dispatchDepth = 0;
    }

    //根据挂载类型创建脚本和闭合 delegate
    private static ScriptInstance? CreateScript(ScriptMount mount, Ens ens)
    {
        if (!TryGetScriptFactory(mount.Type, out ScriptTypeFactory? factory) || factory == null) return null;

        ScriptBehaviour? script = null;
        try
        {
            script = factory.Constructor.Invoke([ens]) as ScriptBehaviour;
            if (script == null) return null;

            script.SetMountId(mount.Id);
            script.enabled = mount.Enabled;
            if (factory.SerializedValueApplier != null)
            {
                factory.SerializedValueApplier.Invoke(script, [mount.Values]);
            }
            else
            {
                ManagedTypeMetadataCache.ApplySerializedValues(script, mount.Values);
            }
            return new ScriptInstance
            {
                Script = script,
                Start = factory.Start?.CreateDelegate<Action>(script),
                Update = factory.Update?.CreateDelegate<Action<float>>(script),
                FixedUpdate = factory.FixedUpdate?.CreateDelegate<Action<float>>(script),
                LateUpdate = factory.LateUpdate?.CreateDelegate<Action<float>>(script),
                DrawGUI = factory.DrawGUI?.CreateDelegate<Action>(script),
                End = factory.End?.CreateDelegate<Action>(script),
                WorldActive = ens.WorldActive,
            };
        }
        catch (Exception exception)
        {
            script?.DetachRuntime();
            Console.Error.WriteLine($"ScriptRuntime: create script '{mount.Type}' failed. {exception}");
            return null;
        }
    }

    //缓存脚本构造函数、序列化入口和具体生命周期方法
    private static bool TryGetScriptFactory(string typeName, out ScriptTypeFactory? factory)
    {
        if (scriptFactories.TryGetValue(typeName, out factory)) return true;

        Type? scriptType = ResolveScriptType(typeName);
        if (scriptType == null || scriptType.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(scriptType))
        {
            Console.WriteLine($"ScriptRuntime: unsupported script type '{typeName}'.");
            return false;
        }

        ConstructorInfo? constructor = scriptType.GetConstructor([typeof(Ens)]);
        if (constructor == null)
        {
            Console.WriteLine($"ScriptRuntime: script '{typeName}' must declare a public constructor with an Ens parameter.");
            return false;
        }

        if (!TryFindLifecycleMethod(scriptType, "OnStart", Type.EmptyTypes, out MethodInfo? start)
            || !TryFindLifecycleMethod(scriptType, "OnUpdate", [typeof(float)], out MethodInfo? update)
            || !TryFindLifecycleMethod(scriptType, "OnFixedUpdate", [typeof(float)], out MethodInfo? fixedUpdate)
            || !TryFindLifecycleMethod(scriptType, "OnLateUpdate", [typeof(float)], out MethodInfo? lateUpdate)
            || !TryFindLifecycleMethod(scriptType, "OnDrawGUI", Type.EmptyTypes, out MethodInfo? drawGUI)
            || !TryFindLifecycleMethod(scriptType, "OnEnd", Type.EmptyTypes, out MethodInfo? end))
        {
            Console.Error.WriteLine($"ScriptRuntime: script '{typeName}' contains an invalid lifecycle method.");
            factory = null;
            return false;
        }

        factory = new ScriptTypeFactory
        {
            Constructor = constructor,
            SerializedValueApplier = scriptType.GetMethod(
                "ApplySerializedValues",
                BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
                binder: null,
                types: [typeof(IReadOnlyDictionary<string, string>)],
                modifiers: null),
            Start = start,
            Update = update,
            FixedUpdate = fixedUpdate,
            LateUpdate = lateUpdate,
            DrawGUI = drawGUI,
            End = end,
        };
        scriptFactories.Add(typeName, factory);
        return true;
    }

    //沿脚本继承链查找最近声明的非虚生命周期方法
    private static bool TryFindLifecycleMethod(
        [DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicMethods | DynamicallyAccessedMemberTypes.NonPublicMethods)] Type scriptType,
        string name,
        Type[] parameters,
        out MethodInfo? lifecycleMethod)
    {
        for (Type? current = scriptType;
             current != null && current != typeof(ScriptBehaviour);
             current = current.BaseType)
        {
            foreach (MethodInfo method in current.GetMethods(
                BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (!string.Equals(method.Name, name, StringComparison.Ordinal)) continue;

                ParameterInfo[] actualParameters = method.GetParameters();
                if (actualParameters.Length != parameters.Length) continue;

                bool signatureMatches = true;
                for (int index = 0; index < parameters.Length; ++index)
                {
                    if (actualParameters[index].ParameterType == parameters[index]) continue;
                    signatureMatches = false;
                    break;
                }
                if (!signatureMatches) continue;

                if (method.IsStatic || method.IsVirtual || method.IsGenericMethod || method.ReturnType != typeof(void))
                {
                    Console.Error.WriteLine(
                        $"ScriptRuntime: lifecycle '{current.FullName}.{name}' must be a non-static, non-virtual void method with the required parameter signature.");
                    lifecycleMethod = null;
                    return false;
                }

                lifecycleMethod = method;
                return true;
            }
        }

        lifecycleMethod = null;
        return true;
    }

    //开始活动脚本并重建所有阶段调用表
    private static void RebuildInvocationLists()
    {
        while (invocationListsDirty)
        {
            invocationListsDirty = false;
            updateInvocations.Clear();
            fixedUpdateInvocations.Clear();
            lateUpdateInvocations.Clear();
            drawGuiInvocations.Clear();

            foreach (ScriptInstance instance in scripts.ToArray())
            {
                if (!IsRunnable(instance)) continue;

                if (!instance.Started)
                {
                    instance.Started = true;
                    Invoke(instance, instance.Start, "OnStart");
                    if (!IsRunnable(instance)) continue;
                }

                if (instance.Update != null) updateInvocations.Add(new(instance, instance.Update));
                if (instance.FixedUpdate != null) fixedUpdateInvocations.Add(new(instance, instance.FixedUpdate));
                if (instance.LateUpdate != null) lateUpdateInvocations.Add(new(instance, instance.LateUpdate));
                if (instance.DrawGUI != null) drawGuiInvocations.Add(new(instance, instance.DrawGUI));
            }
        }
    }

    //判断脚本是否允许参与当前阶段
    private static bool IsRunnable(ScriptInstance instance)
    {
        return !instance.Destroyed && instance.WorldActive && instance.Script.enabled;
    }

    //标记并结束一个脚本
    private static void DestroyScript(ScriptInstance instance)
    {
        if (instance.Destroyed) return;

        instance.Destroyed = true;
        bool callEnd = instance.Started;
        instance.Started = false;
        if (callEnd) Invoke(instance, instance.End, "OnEnd");
        instance.Script.DetachRuntime();
        hasDestroyedScripts = true;
    }

    //清理已销毁脚本及其索引
    private static void RemoveDestroyedScripts()
    {
        if (!hasDestroyedScripts) return;

        scripts.RemoveAll(instance => instance.Destroyed);
        foreach ((EnsId ens, List<ScriptInstance> instances) in scriptsByEns.ToArray())
        {
            instances.RemoveAll(instance => instance.Destroyed);
            if (instances.Count == 0) scriptsByEns.Remove(ens);
        }
        foreach ((ScriptBehaviour script, ScriptInstance instance) in scriptByObject.ToArray())
        {
            if (instance.Destroyed) scriptByObject.Remove(script);
        }
        hasDestroyedScripts = false;
    }

    //在进入阶段前应用上一阶段积累的状态变化
    private static void PreparePhase()
    {
        if (dispatchDepth != 0) return;
        RemoveDestroyedScripts();
        RebuildInvocationLists();
    }

    //结束阶段并清理销毁记录
    private static void CompletePhase()
    {
        dispatchDepth--;
        if (dispatchDepth == 0) RemoveDestroyedScripts();
    }

    //安全执行无参脚本回调
    private static void Invoke(ScriptInstance instance, Action? callback, string phase)
    {
        if (callback == null) return;
        try
        {
            callback();
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"C# script {instance.Script.GetType().FullName} failed in {phase}: {exception}");
        }
    }

    //安全执行有参脚本回调
    private static void Invoke(ScriptInstance instance, Action<float> callback, float deltaTime, string phase)
    {
        try
        {
            callback(deltaTime);
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"C# script {instance.Script.GetType().FullName} failed in {phase}: {exception}");
        }
    }

    //从已加载程序集里解析脚本类型
    [return: DynamicallyAccessedMembers(ScriptMemberTypes)]
    [UnconditionalSuppressMessage("Trimming", "IL2026", Justification = "The game and runtime assemblies are explicit TrimmerRootAssembly entries.")]
    [UnconditionalSuppressMessage("Trimming", "IL2073", Justification = "The game and runtime assemblies are explicit TrimmerRootAssembly entries.")]
    private static Type? ResolveScriptType(string typeName)
    {
        Type? scriptType = gameAssembly?.GetType(typeName, throwOnError: false);
        if (scriptType != null) return scriptType;

        foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            if (ReferenceEquals(assembly, typeof(ScriptRuntime).Assembly)) continue;

            scriptType = assembly.GetType(typeName, throwOnError: false);
            if (scriptType != null) return scriptType;
        }

        return null;
    }

    //获取当前启动 World 的脚本挂载文件
    private static string GetStartupWorldSidecarPath()
    {
        string projectRoot = PathDefines.ContentRoot;
        if (string.IsNullOrWhiteSpace(projectRoot) || !Directory.Exists(projectRoot)) return string.Empty;

        string? projectFile = Directory.EnumerateFiles(projectRoot, "*.oeproj", SearchOption.TopDirectoryOnly).FirstOrDefault();
        if (string.IsNullOrEmpty(projectFile)) return string.Empty;

        string startupWorld = ReadAttribute(File.ReadAllText(projectFile), "startupWorld");
        if (string.IsNullOrWhiteSpace(startupWorld)) return string.Empty;

        string worldPath = Path.Combine(projectRoot, startupWorld.Replace('/', Path.DirectorySeparatorChar));
        return Path.GetFullPath(worldPath) + ".scripts.json";
    }

    //读取项目文件中的单行属性
    private static string ReadAttribute(string text, string name)
    {
        string token = name + "=\"";
        int start = text.IndexOf(token, StringComparison.Ordinal);
        if (start < 0) return string.Empty;

        start += token.Length;
        int end = text.IndexOf('"', start);
        return end > start ? text[start..end] : string.Empty;
    }

    //读取脚本挂载列表
    private static IEnumerable<ScriptMount> ReadScriptMounts(string sidecarPath)
    {
        using JsonDocument document = JsonDocument.Parse(File.ReadAllText(sidecarPath));
        if (!document.RootElement.TryGetProperty("scripts", out JsonElement scriptsElement)) yield break;
        if (scriptsElement.ValueKind != JsonValueKind.Array) yield break;

        foreach (JsonElement element in scriptsElement.EnumerateArray())
        {
            ScriptMount? mount = ReadScriptMount(element);
            if (mount != null) yield return mount;
        }
    }

    //读取单个脚本挂载项
    private static ScriptMount? ReadScriptMount(JsonElement element)
    {
        string stableId = element.TryGetProperty("stableId", out JsonElement stableIdElement) ? stableIdElement.GetString() ?? string.Empty : string.Empty;
        string type = element.TryGetProperty("type", out JsonElement typeElement) ? typeElement.GetString() ?? string.Empty : string.Empty;
        if (string.IsNullOrWhiteSpace(stableId) || string.IsNullOrWhiteSpace(type)) return null;

        string id = element.TryGetProperty("id", out JsonElement idElement) ? idElement.GetString() ?? string.Empty : string.Empty;
        if (string.IsNullOrWhiteSpace(id)) id = Guid.NewGuid().ToString("N");
        bool enabled = !element.TryGetProperty("enabled", out JsonElement enabledElement)
            || enabledElement.ValueKind != JsonValueKind.False;

        ScriptMount mount = new() { Id = id, StableId = stableId, Type = StripAssemblyName(type), Enabled = enabled };
        if (!element.TryGetProperty("values", out JsonElement valuesElement)) return mount;
        if (valuesElement.ValueKind != JsonValueKind.Object) return mount;

        foreach (JsonProperty property in valuesElement.EnumerateObject())
        {
            mount.Values[property.Name] = ReadSerializedValue(property.Value);
        }

        return mount;
    }

    //读取序列化字段文本
    private static string ReadSerializedValue(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object && element.TryGetProperty("value", out JsonElement valueElement))
        {
            return GetJsonValueText(valueElement);
        }

        return GetJsonValueText(element);
    }

    //转换 Json 字段文本
    private static string GetJsonValueText(JsonElement element)
    {
        return element.ValueKind == JsonValueKind.String ? element.GetString() ?? string.Empty : element.GetRawText();
    }

    //去掉脚本类型中的程序集后缀
    private static string StripAssemblyName(string typeName)
    {
        string value = typeName.Trim();
        int commaIndex = value.IndexOf(',');
        return commaIndex >= 0 ? value[..commaIndex].Trim() : value;
    }
}

/// <summary>读取脚本挂载中的基础字段值。</summary>
public static class ScriptValueReader
{
    /// <summary>读取 string 字段。</summary>
    public static bool TryGetString(IReadOnlyDictionary<string, string> values, string name, out string value)
    {
        if (values.TryGetValue(name, out string? text))
        {
            value = text;
            return true;
        }

        value = string.Empty;
        return false;
    }

    /// <summary>读取 bool 字段。</summary>
    public static bool TryGetBool(IReadOnlyDictionary<string, string> values, string name, out bool value)
    {
        value = false;
        return values.TryGetValue(name, out string? text) && bool.TryParse(text, out value);
    }

    /// <summary>读取 int 字段。</summary>
    public static bool TryGetInt(IReadOnlyDictionary<string, string> values, string name, out int value)
    {
        value = 0;
        return values.TryGetValue(name, out string? text)
            && int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value);
    }

    /// <summary>读取 float 字段。</summary>
    public static bool TryGetFloat(IReadOnlyDictionary<string, string> values, string name, out float value)
    {
        value = 0.0f;
        return values.TryGetValue(name, out string? text)
            && float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
    }

    /// <summary>读取 vector3 字段。</summary>
    public static bool TryGetVector3(IReadOnlyDictionary<string, string> values, string name, out vector3 value)
    {
        value = new vector3();
        if (!values.TryGetValue(name, out string? text)) return false;

        string[] parts = text.Split([' ', ',', ';'], StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length != 3) return false;
        if (!float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out float x)) return false;
        if (!float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out float y)) return false;
        if (!float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out float z)) return false;

        value = new vector3(x, y, z);
        return true;
    }
}
