using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;

namespace Orbeden;

/// <summary>管理绑定原生 ScriptBehaviour 宿主的 C# 脚本和预解析生命周期表。</summary>
internal static class ScriptRuntime
{
    private const DynamicallyAccessedMemberTypes ScriptMembers =
        DynamicallyAccessedMemberTypes.PublicConstructors |
        DynamicallyAccessedMemberTypes.PublicMethods |
        DynamicallyAccessedMemberTypes.NonPublicMethods;

    private sealed class GameLoadContext(string assemblyPath) : AssemblyLoadContext(isCollectible: true)
    {
        private readonly AssemblyDependencyResolver resolver = new(assemblyPath);

        [UnconditionalSuppressMessage("Trimming", "IL2026",
            Justification = "Only the non-trimmed Editor CLR runtime loads files dynamically.")]
        internal Assembly LoadFile(string path)
        {
            using FileStream assembly = File.OpenRead(path);
            string symbolsPath = Path.ChangeExtension(path, ".pdb");
            if (!File.Exists(symbolsPath)) return LoadFromStream(assembly);
            using FileStream symbols = File.OpenRead(symbolsPath);
            return LoadFromStream(assembly, symbols);
        }

        protected override Assembly? Load(AssemblyName name)
        {
            Assembly core = typeof(ScriptBehaviour).Assembly;
            if (name.Name == core.GetName().Name) return core;
            string? path = resolver.ResolveAssemblyToPath(name);
            return path == null ? null : LoadFile(path);
        }
    }

    private sealed class ScriptFactory
    {
        internal ConstructorInfo Constructor = null!;
        internal MethodInfo? Start;
        internal MethodInfo? Update;
        internal MethodInfo? FixedUpdate;
        internal MethodInfo? LateUpdate;
        internal MethodInfo? DrawGUI;
        internal MethodInfo? End;
    }

    private sealed class ScriptInstance
    {
        internal IntPtr Host;
        internal ScriptBehaviour Script = null!;
        internal Action? Start;
        internal Action<float>? Update;
        internal Action<float>? FixedUpdate;
        internal Action<float>? LateUpdate;
        internal Action? DrawGUI;
        internal Action? End;
        internal bool WorldActive;
        internal bool Enabled;
        internal bool Started;
        internal bool Destroyed;
    }

    private readonly record struct TimedCall(ScriptInstance Instance, Action<float> Callback);
    private readonly record struct Call(ScriptInstance Instance, Action Callback);

    private static readonly List<ScriptInstance> scripts = [];
    private static readonly Dictionary<IntPtr, ScriptInstance> scriptsByHost = [];
    private static readonly Dictionary<ScriptBehaviour, ScriptInstance> scriptsByObject = new(ReferenceEqualityComparer.Instance);
    private static readonly Dictionary<EnsId, List<ScriptInstance>> scriptsByEns = [];
    private static readonly Dictionary<string, ScriptFactory> factories = new(StringComparer.Ordinal);
    private static readonly HashSet<IntPtr> pendingAdds = [];
    private static readonly List<TimedCall> updates = [];
    private static readonly List<TimedCall> fixedUpdates = [];
    private static readonly List<TimedCall> lateUpdates = [];
    private static readonly List<Call> guiCalls = [];
    private static GameLoadContext? gameContext;
    private static Assembly? gameAssembly;
    private static bool callsDirty;
    private static bool hasDestroyed;
    private static bool rebuilding;
    private static int dispatchDepth;
    private static bool shuttingDown;

    /// <summary>连接原生 API，并为当前 World 已有的全部托管宿主创建 Wrapper。</summary>
    public static void Initialize(IntPtr nativeApi)
    {
        ShutdownScripts();
        OrbedenCoreRuntime.Initialize(nativeApi);
        ManagedScriptInterop.Shutdown();
        ScriptRuntimeRegistry.Clear();
        ManagedScriptInterop.Initialize();

        foreach (IntPtr host in ScriptBehaviour.GetManagedHosts()) CreateForHost(host);
        callsDirty = true;
        RebuildCalls();
    }

    /// <summary>执行 Update 表。</summary>
    public static void Update(float deltaTime) => Dispatch(updates, deltaTime, "OnUpdate");

    /// <summary>执行 FixedUpdate 表。</summary>
    public static void FixedUpdate(float fixedDeltaTime) =>
        Dispatch(fixedUpdates, fixedDeltaTime, "OnFixedUpdate");

    /// <summary>执行 LateUpdate 表。</summary>
    public static void LateUpdate(float deltaTime) =>
        Dispatch(lateUpdates, deltaTime, "OnLateUpdate");

    /// <summary>执行 DrawGUI 表。</summary>
    public static void DrawGUI()
    {
        PreparePhase();
        ++dispatchDepth;
        try
        {
            foreach (Call call in guiCalls)
            {
                if (IsRunnable(call.Instance))
                    Invoke(call.Instance, call.Callback, "OnDrawGUI");
            }
        }
        finally { CompletePhase(); }
    }

    /// <summary>创建一个具有独立原生组件身份的托管脚本。</summary>
    internal static ScriptBehaviour? AddManagedScript(EnsId ens, Type type)
    {
        if (ens.IsNull || type.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(type))
            return null;
        string? typeName = type.FullName;
        if (string.IsNullOrEmpty(typeName) || !TryGetFactory(typeName, out _)) return null;

        IntPtr host = ScriptBehaviour.CreateManagedHost(ens, typeName);
        if (host == IntPtr.Zero) return null;
        if (!scriptsByHost.TryGetValue(host, out ScriptInstance? instance))
            instance = CreateForHost(host);
        if (instance == null)
        {
            ScriptBehaviour.RemoveManagedHost(host);
            return null;
        }

        ManagedTypeMetadataCache.WriteMissingHostFields(instance.Script, host);
        return instance.Script;
    }

    /// <summary>移除 Wrapper 对应的原生宿主组件。</summary>
    internal static bool RemoveManagedScript(ScriptBehaviour script)
    {
        IntPtr host = script.NativePtr;
        return scriptsByObject.ContainsKey(script) && host != IntPtr.Zero
            && ScriptBehaviour.RemoveManagedHost(host);
    }

    /// <summary>响应原生宿主挂载事件。</summary>
    internal static InteropStatus OnHostAttached(IntPtr host)
    {
        if (shuttingDown || host == IntPtr.Zero) return InteropStatus.InvalidArgument;
        if (scriptsByHost.ContainsKey(host))
        {
            callsDirty = true;
            return InteropStatus.Ok;
        }
        if (dispatchDepth != 0 || rebuilding)
        {
            pendingAdds.Add(host);
            return InteropStatus.Ok;
        }
        return CreateForHost(host) == null ? InteropStatus.NotFound : InteropStatus.Ok;
    }

    /// <summary>响应原生宿主移除事件，并立即使旧代理失效。</summary>
    internal static InteropStatus OnHostDetached(IntPtr host)
    {
        pendingAdds.Remove(host);
        if (!scriptsByHost.TryGetValue(host, out ScriptInstance? instance))
            return host == IntPtr.Zero ? InteropStatus.InvalidArgument : InteropStatus.NotFound;
        DestroyScript(instance);
        callsDirty = true;
        if (dispatchDepth == 0 && !rebuilding) RemoveDestroyed();
        return InteropStatus.Ok;
    }

    /// <summary>响应原生宿主 enabled 变化。</summary>
    internal static InteropStatus OnHostEnabledChanged(IntPtr host)
    {
        if (!scriptsByHost.TryGetValue(host, out ScriptInstance? instance)) return InteropStatus.NotFound;
        instance.Enabled = ScriptBehaviour.GetHostEnabled(host);
        callsDirty = true;
        return InteropStatus.Ok;
    }

    /// <summary>把宿主字段的新值同步到活跃 Wrapper。</summary>
    internal static InteropStatus OnHostFieldChanged(IntPtr host, string fieldName)
    {
        if (!scriptsByHost.TryGetValue(host, out ScriptInstance? instance))
            return InteropStatus.NotFound;
        if (!ManagedTypeMetadataCache.Get(instance.Script.GetType()).Fields.ContainsKey(fieldName))
            return InteropStatus.NotFound;
        return ManagedTypeMetadataCache.ApplyHostField(instance.Script, host, fieldName)
            ? InteropStatus.Ok
            : InteropStatus.InvocationFailed;
    }

    /// <summary>响应 Ens 世界活动状态变化。</summary>
    public static void OnEnsWorldActiveChanged(EnsId ens, bool active)
    {
        if (!scriptsByEns.TryGetValue(ens, out List<ScriptInstance>? values)) return;
        foreach (ScriptInstance value in values) value.WorldActive = active;
        callsDirty = true;
    }

    /// <summary>响应 Ens 销毁事件。</summary>
    public static void OnEnsDestroyed(EnsId ens)
    {
        if (!scriptsByEns.TryGetValue(ens, out List<ScriptInstance>? values)) return;
        foreach (ScriptInstance value in values.ToArray()) DestroyScript(value);
        callsDirty = true;
        if (dispatchDepth == 0 && !rebuilding) RemoveDestroyed();
    }

    /// <summary>结束全部 Wrapper；原生宿主仍由 World 负责销毁。</summary>
    public static void Shutdown()
    {
        ShutdownScripts();
        ScriptRuntimeRegistry.Clear();
        ManagedScriptInterop.Shutdown();
        factories.Clear();
        ManagedTypeMetadataCache.Clear();
        gameAssembly = null;
        if (gameContext != null)
        {
            gameContext.Unload();
            gameContext = null;
        }
        ScriptBehaviour.InitializeNativeApi(default);
    }

    /// <summary>加载 Editor CLR 模式使用的游戏程序集。</summary>
    internal static bool LoadGameAssembly(string assemblyPath)
    {
        if (scripts.Count != 0 || string.IsNullOrWhiteSpace(assemblyPath)
            || !File.Exists(assemblyPath))
            return false;
        try
        {
            gameAssembly = null;
            gameContext?.Unload();
            string path = Path.GetFullPath(assemblyPath);
            gameContext = new GameLoadContext(path);
            gameAssembly = gameContext.LoadFile(path);
            factories.Clear();
            ManagedTypeMetadataCache.Clear();
            return true;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"ScriptRuntime: game assembly load failed. {exception}");
            gameAssembly = null;
            gameContext?.Unload();
            gameContext = null;
            return false;
        }
    }

    //执行一个带时间参数的阶段表。
    private static void Dispatch(List<TimedCall> calls, float deltaTime, string phase)
    {
        PreparePhase();
        ++dispatchDepth;
        try
        {
            foreach (TimedCall call in calls)
            {
                if (!IsRunnable(call.Instance)) continue;
                try { call.Callback(deltaTime); }
                catch (Exception exception) { LogFailure(call.Instance, phase, exception); }
            }
        }
        finally { CompletePhase(); }
    }

    //在原生宿主上构造 Wrapper，并把生命周期方法绑定为闭合 delegate。
    private static ScriptInstance? CreateForHost(IntPtr host)
    {
        if (shuttingDown || host == IntPtr.Zero) return null;
        pendingAdds.Remove(host);
        if (scriptsByHost.TryGetValue(host, out ScriptInstance? old)) return old;

        EnsId ensId = ScriptBehaviour.GetHostEns(host);
        string typeName = ScriptBehaviour.GetHostTypeName(host);
        if (ensId.IsNull || string.IsNullOrEmpty(typeName)
            || !TryGetFactory(typeName, out ScriptFactory? factory)
            || factory == null)
            return null;

        Ens ens = Ens.FromId(ensId);
        if (!ens.IsValid) return null;
        ScriptBehaviour? script = null;
        try
        {
            using (ScriptBehaviour.BeginConstruction(ensId, host))
                script = factory.Constructor.Invoke([ens]) as ScriptBehaviour;
            if (script == null) return null;

            ScriptInstance instance = new()
            {
                Host = host,
                Script = script,
                Start = factory.Start?.CreateDelegate<Action>(script),
                Update = factory.Update?.CreateDelegate<Action<float>>(script),
                FixedUpdate = factory.FixedUpdate?.CreateDelegate<Action<float>>(script),
                LateUpdate = factory.LateUpdate?.CreateDelegate<Action<float>>(script),
                DrawGUI = factory.DrawGUI?.CreateDelegate<Action>(script),
                End = factory.End?.CreateDelegate<Action>(script),
                WorldActive = ens.WorldActive,
                Enabled = ScriptBehaviour.GetHostEnabled(host),
            };
            Register(instance);
            ManagedTypeMetadataCache.ApplyHostFields(script, host);
            ManagedTypeMetadataCache.WriteMissingHostFields(script, host);
            callsDirty = true;
            return instance;
        }
        catch (Exception exception)
        {
            if (scriptsByHost.TryGetValue(host, out ScriptInstance? failed)) DestroyScript(failed);
            else script?.DetachRuntime();
            Console.Error.WriteLine($"ScriptRuntime: create '{typeName}' failed. {exception}");
            return null;
        }
    }

    //注册宿主、Wrapper、ObjectId 和 Ens 索引。
    internal static ScriptBehaviour? GetOrCreateHost(IntPtr host) => CreateForHost(host)?.Script;

    private static void Register(ScriptInstance instance)
    {
        scripts.Add(instance);
        scriptsByHost.Add(instance.Host, instance);
        scriptsByObject.Add(instance.Script, instance);
        ScriptRuntimeRegistry.Register(instance.Script);
        if (!scriptsByEns.TryGetValue(instance.Script.EnsId, out List<ScriptInstance>? values))
        {
            values = [];
            scriptsByEns.Add(instance.Script.EnsId, values);
        }
        values.Add(instance);
    }

    //缓存脚本构造函数和具体的非虚生命周期方法。
    private static bool TryGetFactory(string typeName, out ScriptFactory? factory)
    {
        if (factories.TryGetValue(typeName, out factory)) return true;
        Type? type = ResolveType(typeName);
        if (type == null || type.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(type))
            return false;

        ConstructorInfo? constructor = type.GetConstructor([typeof(Ens)]);
        if (constructor == null)
        {
            Console.Error.WriteLine($"ScriptRuntime: '{typeName}' needs a public (Ens ens) constructor.");
            return false;
        }

        if (!FindLifecycle(type, "OnStart", Type.EmptyTypes, out MethodInfo? start)
            || !FindLifecycle(type, "OnUpdate", [typeof(float)], out MethodInfo? update)
            || !FindLifecycle(type, "OnFixedUpdate", [typeof(float)], out MethodInfo? fixedUpdate)
            || !FindLifecycle(type, "OnLateUpdate", [typeof(float)], out MethodInfo? lateUpdate)
            || !FindLifecycle(type, "OnDrawGUI", Type.EmptyTypes, out MethodInfo? drawGui)
            || !FindLifecycle(type, "OnEnd", Type.EmptyTypes, out MethodInfo? end))
        {
            factory = null;
            return false;
        }

        factory = new ScriptFactory
        {
            Constructor = constructor,
            Start = start,
            Update = update,
            FixedUpdate = fixedUpdate,
            LateUpdate = lateUpdate,
            DrawGUI = drawGui,
            End = end,
        };
        factories.Add(typeName, factory);
        return true;
    }

    //沿继承链查找最近声明的约定生命周期方法。
    private static bool FindLifecycle(
        [DynamicallyAccessedMembers(
            DynamicallyAccessedMemberTypes.PublicMethods |
            DynamicallyAccessedMemberTypes.NonPublicMethods)] Type type,
        string name,
        Type[] parameters,
        out MethodInfo? result)
    {
        for (Type? current = type;
             current != null && current != typeof(ScriptBehaviour);
             current = current.BaseType)
        {
            foreach (MethodInfo method in current.GetMethods(
                BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public |
                BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (method.Name != name) continue;
                ParameterInfo[] actual = method.GetParameters();
                if (actual.Length != parameters.Length
                    || !actual.Select(value => value.ParameterType).SequenceEqual(parameters))
                    continue;
                if (method.IsStatic || method.IsVirtual || method.IsGenericMethod
                    || method.ReturnType != typeof(void))
                {
                    Console.Error.WriteLine(
                        $"ScriptRuntime: '{current.FullName}.{name}' must be a non-static, non-virtual void method.");
                    result = null;
                    return false;
                }
                result = method;
                return true;
            }
        }
        result = null;
        return true;
    }

    //启动首次变为活动的脚本，并重建紧凑阶段表。
    private static void RebuildCalls()
    {
        if (!callsDirty) return;
        rebuilding = true;
        try
        {
            callsDirty = false;
            Dictionary<IntPtr, int> order = [];
            foreach (IntPtr host in ScriptBehaviour.GetManagedHosts()) order[host] = order.Count;
            scripts.Sort((left, right) => order.GetValueOrDefault(left.Host, int.MaxValue)
                .CompareTo(order.GetValueOrDefault(right.Host, int.MaxValue)));
            updates.Clear();
            fixedUpdates.Clear();
            lateUpdates.Clear();
            guiCalls.Clear();
            foreach (ScriptInstance instance in scripts.ToArray())
            {
                if (!IsRunnable(instance)) continue;
                if (!instance.Started)
                {
                    instance.Started = true;
                    Invoke(instance, instance.Start, "OnStart");
                    if (!IsRunnable(instance)) continue;
                }
                if (instance.Update != null) updates.Add(new(instance, instance.Update));
                if (instance.FixedUpdate != null) fixedUpdates.Add(new(instance, instance.FixedUpdate));
                if (instance.LateUpdate != null) lateUpdates.Add(new(instance, instance.LateUpdate));
                if (instance.DrawGUI != null) guiCalls.Add(new(instance, instance.DrawGUI));
            }
        }
        finally { rebuilding = false; }
    }

    //判断脚本是否可参与阶段。
    private static bool IsRunnable(ScriptInstance instance)
    {
        return !instance.Destroyed && instance.WorldActive
            && instance.Enabled;
    }

    //保证已 Start 的脚本只执行一次 End，并使注册表句柄立即失效。
    private static void DestroyScript(ScriptInstance instance)
    {
        if (instance.Destroyed) return;
        instance.Destroyed = true;
        if (instance.Started) Invoke(instance, instance.End, "OnEnd");
        instance.Started = false;
        scriptsByObject.Remove(instance.Script);
        instance.Script.DetachRuntime();
        scriptsByHost.Remove(instance.Host);
        hasDestroyed = true;
    }

    //移除所有已销毁记录。
    private static void RemoveDestroyed()
    {
        if (!hasDestroyed) return;
        scripts.RemoveAll(value => value.Destroyed);
        foreach ((EnsId ens, List<ScriptInstance> values) in scriptsByEns.ToArray())
        {
            values.RemoveAll(value => value.Destroyed);
            if (values.Count == 0) scriptsByEns.Remove(ens);
        }
        foreach ((ScriptBehaviour script, ScriptInstance value) in scriptsByObject.ToArray())
        {
            if (value.Destroyed) scriptsByObject.Remove(script);
        }
        hasDestroyed = false;
    }

    //应用上一阶段产生的结构变化。
    private static void PreparePhase()
    {
        if (dispatchDepth != 0) return;
        RemoveDestroyed();
        if (pendingAdds.Count != 0)
        {
            IntPtr[] changes = [.. pendingAdds];
            foreach (IntPtr host in changes)
            {
                if (pendingAdds.Remove(host)) CreateForHost(host);
            }
        }
        RebuildCalls();
    }

    private static void CompletePhase()
    {
        --dispatchDepth;
        if (dispatchDepth == 0) RemoveDestroyed();
    }

    //安全执行无参生命周期。
    private static void Invoke(ScriptInstance instance, Action? callback, string phase)
    {
        if (callback == null) return;
        try { callback(); }
        catch (Exception exception) { LogFailure(instance, phase, exception); }
    }

    private static void LogFailure(ScriptInstance instance, string phase, Exception exception)
    {
        Console.Error.WriteLine(
            $"C# script {instance.Script.GetType().FullName} failed in {phase}: {exception}");
    }

    //结束 Wrapper 和阶段表，不销毁 World 中的宿主组件。
    private static void ShutdownScripts()
    {
        shuttingDown = true;
        pendingAdds.Clear();
        foreach (ScriptInstance instance in scripts.ToArray().Reverse()) DestroyScript(instance);
        scripts.Clear();
        scriptsByHost.Clear();
        scriptsByObject.Clear();
        scriptsByEns.Clear();
        updates.Clear();
        fixedUpdates.Clear();
        lateUpdates.Clear();
        guiCalls.Clear();
        callsDirty = false;
        hasDestroyed = false;
        rebuilding = false;
        dispatchDepth = 0;
        shuttingDown = false;
    }

    [return: DynamicallyAccessedMembers(ScriptMembers)]
    [UnconditionalSuppressMessage("Trimming", "IL2026",
        Justification = "Game assemblies are explicit TrimmerRootAssembly entries.")]
    [UnconditionalSuppressMessage("Trimming", "IL2073",
        Justification = "Game assemblies are explicit TrimmerRootAssembly entries.")]
    private static Type? ResolveType(string typeName)
    {
        Type? type = gameAssembly?.GetType(typeName, throwOnError: false);
        if (type != null) return type;
        foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            if (ReferenceEquals(assembly, typeof(ScriptRuntime).Assembly)) continue;
            type = assembly.GetType(typeName, throwOnError: false);
            if (type != null) return type;
        }
        return null;
    }
}
