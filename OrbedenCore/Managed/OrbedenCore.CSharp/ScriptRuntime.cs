using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Reflection;

namespace Orbeden;

/// <summary>管理绑定原生 Script 宿主的 C# 脚本和预解析生命周期表。</summary>
internal static class ScriptRuntime
{
    private const DynamicallyAccessedMemberTypes ScriptMembers =
        DynamicallyAccessedMemberTypes.PublicConstructors |
        DynamicallyAccessedMemberTypes.PublicMethods |
        DynamicallyAccessedMemberTypes.NonPublicMethods;

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
        internal Script Script = null!;
        internal IManagedComponentLifecycle? Lifecycle;
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
        //是否已完成附着通知，以及最近一次发送给扩展的活动状态。
        internal bool Attached;
        internal bool LastActive;
        internal bool HasActiveState;
    }

    private readonly record struct TimedCall(ScriptInstance Instance, Action<float> Callback);
    private readonly record struct Call(ScriptInstance Instance, Action Callback);

    private static readonly List<ScriptInstance> scripts = [];
    private static readonly Dictionary<IntPtr, ScriptInstance> scriptsByHost = [];
    private static readonly Dictionary<EnsId, List<ScriptInstance>> scriptsByEns = [];
    private static readonly Dictionary<string, ScriptFactory> factories = new(StringComparer.Ordinal);
    private static readonly HashSet<IntPtr> pendingAdds = [];
    private static readonly List<TimedCall> updates = [];
    private static readonly List<TimedCall> fixedUpdates = [];
    private static readonly List<TimedCall> lateUpdates = [];
    private static readonly List<Call> guiCalls = [];
    private static bool callsDirty;
    private static bool rebuilding;
    private static int dispatchDepth;
    private static bool shuttingDown;
    private static ScriptExecutionMode executionMode = ScriptExecutionMode.Play;
    private static ulong worldRevision;

    /// <summary>连接原生 API，并为当前 World 已有的全部托管宿主创建 Wrapper。</summary>
    public static void Initialize(IntPtr nativeApi, uint mode)
    {
        executionMode = mode == (uint)ScriptExecutionMode.Editor
            ? ScriptExecutionMode.Editor
            : ScriptExecutionMode.Play;
        ShutdownScripts();
        OrbedenCoreRuntime.Initialize(nativeApi);
        ManagedScriptInterop.Shutdown();
        ScriptRuntimeRegistry.Clear();
        ManagedScriptInterop.Initialize();
        ManagedTypeMetadataCache.HostFieldsChanged = OnHostFieldsChanged;

        //两阶段实例构造：先让全部可解析宿主取得包装并登记，再统一恢复字段与发送附着通知。
        //循环引用在第二阶段通过已登记包装解析，不递归构造对端。
        List<ScriptInstance> constructed = [];
        foreach (IntPtr host in Script.GetManagedHosts())
        {
            ScriptInstance? instance = ConstructHost(host);
            if (instance != null && !instance.Attached) constructed.Add(instance);
        }
        foreach (ScriptInstance instance in constructed) ApplyHostState(instance);
        foreach (ScriptInstance instance in constructed) AttachHost(instance);
        callsDirty = true;
        RebuildCalls();

        //帧系统在全部包装建立之后附着：此时索引可以安全读取组件。
        ++worldRevision;
        if (worldRevision == 0) worldRevision = 1;
        ManagedFrameSystems.AttachWorld(worldRevision, executionMode == ScriptExecutionMode.Editor);
    }

    /// <summary>处理托管输入阶段；不受模拟暂停门控。</summary>
    public static void ProcessInput(float deltaTime) => ManagedFrameSystems.ProcessInput(deltaTime);

    /// <summary>准备托管渲染阶段；不受暂停门控，编辑模式同样执行。</summary>
    public static void PrepareRender(float deltaTime) => ManagedFrameSystems.PrepareRender(deltaTime);

    //编辑模式不执行游戏生命周期：只保留包装与帧系统。
    private static bool SkipsGameLifecycle => executionMode == ScriptExecutionMode.Editor;

    /// <summary>执行 Update 表。</summary>
    public static void Update(float deltaTime)
    {
        if (SkipsGameLifecycle) return;
        Dispatch(updates, deltaTime, "OnUpdate");
    }

    /// <summary>执行 FixedUpdate 表。</summary>
    public static void FixedUpdate(float fixedDeltaTime)
    {
        if (SkipsGameLifecycle) return;
        Dispatch(fixedUpdates, fixedDeltaTime, "OnFixedUpdate");
    }

    /// <summary>执行 LateUpdate 表。</summary>
    public static void LateUpdate(float deltaTime)
    {
        if (SkipsGameLifecycle) return;
        Dispatch(lateUpdates, deltaTime, "OnLateUpdate");
    }

    /// <summary>执行 DrawGUI 表。</summary>
    public static void DrawGUI()
    {
        if (SkipsGameLifecycle) return;
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
        finally { --dispatchDepth; }
    }

    /// <summary>创建一个具有独立原生组件身份的托管脚本。</summary>
    internal static Script? AddManagedScript(EnsId ens, Type type)
    {
        if (ens.IsNull || type.IsAbstract || !NativeBindingRuntime.IsManagedScript(type))
            return null;
        string? typeName = type.FullName;
        if (string.IsNullOrEmpty(typeName) || !TryGetFactory(typeName, out _)) return null;

        IntPtr host = Script.CreateManagedHost(ens, typeName);
        if (host == IntPtr.Zero) return null;
        if (!scriptsByHost.TryGetValue(host, out ScriptInstance? instance))
            instance = CreateAndAttach(host);
        if (instance == null)
        {
            Script.RemoveManagedHost(host);
            return null;
        }

        ManagedTypeMetadataCache.SyncHostFields(instance.Script, host);
        return instance.Script;
    }

    /// <summary>移除 Wrapper 对应的原生宿主组件。</summary>
    internal static bool RemoveManagedScript(Script script)
    {
        IntPtr host = script.NativePtr;
        return scriptsByHost.TryGetValue(host, out ScriptInstance? instance)
            && ReferenceEquals(instance.Script, script)
            && Script.RemoveManagedHost(host);
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
        return CreateAndAttach(host) == null ? InteropStatus.NotFound : InteropStatus.Ok;
    }

    /// <summary>响应原生宿主移除事件，并立即使旧代理失效。</summary>
    internal static InteropStatus OnHostDetached(IntPtr host)
    {
        pendingAdds.Remove(host);
        if (!scriptsByHost.TryGetValue(host, out ScriptInstance? instance))
            return host == IntPtr.Zero ? InteropStatus.InvalidArgument : InteropStatus.NotFound;
        DestroyScript(instance);
        return InteropStatus.Ok;
    }

    /// <summary>响应原生宿主 enabled 变化。</summary>
    internal static InteropStatus OnHostEnabledChanged(IntPtr host)
    {
        if (!scriptsByHost.TryGetValue(host, out ScriptInstance? instance)) return InteropStatus.NotFound;
        instance.Enabled = Script.GetHostEnabled(host);
        SendActive(instance, IsRunnable(instance));
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
        if (!ManagedTypeMetadataCache.ApplyHostField(instance.Script, host, fieldName))
            return InteropStatus.InvocationFailed;
        NotifyFieldsChanged(instance);
        return InteropStatus.Ok;
    }

    /// <summary>响应 Ens 世界活动状态变化。</summary>
    public static void OnEnsWorldActiveChanged(EnsId ens, bool active)
    {
        if (!scriptsByEns.TryGetValue(ens, out List<ScriptInstance>? values)) return;
        foreach (ScriptInstance value in values)
        {
            value.WorldActive = active;
            SendActive(value, IsRunnable(value));
        }
        callsDirty = true;
    }

    /// <summary>响应 Ens 销毁事件。</summary>
    public static void OnEnsDestroyed(EnsId ens)
    {
        if (!scriptsByEns.TryGetValue(ens, out List<ScriptInstance>? values)) return;
        foreach (ScriptInstance value in values.ToArray()) DestroyScript(value);
    }

    /// <summary>结束全部 Wrapper；原生宿主仍由 World 负责销毁。</summary>
    public static void Shutdown()
    {
        //帧系统先释放：UI 上下文要在包装断开之前撤销输入与缓存引用。
        ManagedFrameSystems.DetachWorld();
        ShutdownScripts();
        ScriptRuntimeRegistry.Clear();
        ManagedTypeMetadataCache.HostFieldsChanged = null;
        ManagedScriptInterop.Shutdown();
        factories.Clear();
        ManagedTypeMetadataCache.Clear();
        //程序集本身由 ManagedAssemblySession 持有：进出 Play 换的是世界实例，不重新加载程序集。
        Script.InitializeNativeApi(default);
    }

    /// <summary>当前是否仍有活动脚本宿主，即世界尚未分离。</summary>
    internal static bool HasAttachedWorld => scripts.Count != 0;

    /// <summary>会话卸载前清空工厂与元数据缓存；脚本实例在此之前已经断开。</summary>
    internal static void OnAssemblySessionUnloaded()
    {
        //注册表里保存的是游戏程序集提供的工厂与实例，必须先清空才允许释放加载上下文。
        ManagedFrameSystems.Clear();
        factories.Clear();
        ManagedTypeMetadataCache.Clear();
        ManagedScriptInterop.ClearMembers();
    }

    /// <summary>加载 Editor CLR 模式使用的游戏程序集。</summary>
    internal static bool LoadGameAssembly(string assemblyPath)
    {
        if (scripts.Count != 0 || string.IsNullOrWhiteSpace(assemblyPath)
            || !File.Exists(assemblyPath))
            return false;
        if (!ManagedAssemblySession.Load(assemblyPath, null)) return false;
        factories.Clear();
        ManagedTypeMetadataCache.Clear();
        return true;
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
        finally { --dispatchDepth; }
    }

    //第一阶段：构造 Wrapper 并登记身份，只初始化字段，不读取宿主字段、不发送扩展通知。
    //找不到托管类型时返回 null 并保留宿主与原字段，供编辑器显示 Missing Script。
    private static ScriptInstance? ConstructHost(IntPtr host)
    {
        if (shuttingDown || host == IntPtr.Zero) return null;
        pendingAdds.Remove(host);
        if (scriptsByHost.TryGetValue(host, out ScriptInstance? old)) return old;

        EnsId ensId = Script.GetHostEns(host);
        string typeName = Script.GetHostTypeName(host);
        if (ensId.IsNull || string.IsNullOrEmpty(typeName)
            || !TryGetFactory(typeName, out ScriptFactory? factory)
            || factory == null)
            return null;

        Ens ens = Ens.FromId(ensId);
        if (!ens.IsValid) return null;
        Script? script = null;
        try
        {
            using (Script.BeginConstruction(ensId, host))
                script = factory.Constructor.Invoke([ens]) as Script;
            if (script == null) return null;

            ScriptInstance instance = new()
            {
                Host = host,
                Script = script,
                Lifecycle = script as IManagedComponentLifecycle,
                Start = factory.Start?.CreateDelegate<Action>(script),
                Update = factory.Update?.CreateDelegate<Action<float>>(script),
                FixedUpdate = factory.FixedUpdate?.CreateDelegate<Action<float>>(script),
                LateUpdate = factory.LateUpdate?.CreateDelegate<Action<float>>(script),
                DrawGUI = factory.DrawGUI?.CreateDelegate<Action>(script),
                End = factory.End?.CreateDelegate<Action>(script),
                WorldActive = ens.WorldActive,
                Enabled = Script.GetHostEnabled(host),
            };
            Register(instance);
            callsDirty = true;
            return instance;
        }
        catch (Exception exception)
        {
            if (scriptsByHost.TryGetValue(host, out ScriptInstance? failed)) DestroyScript(failed);
            else script?.DisconnectNative();
            Console.Error.WriteLine($"ScriptRuntime: create '{typeName}' failed. {exception}");
            return null;
        }
    }

    //第二阶段：恢复宿主字段并解析引用；此时全部同批包装都已登记，循环引用可直接命中。
    //单个实例失败只断开它自己，不阻塞其他实例。
    private static void ApplyHostState(ScriptInstance instance)
    {
        if (instance.Destroyed) return;
        try
        {
            ManagedTypeMetadataCache.ApplyHostFields(instance.Script, instance.Host);
            ManagedTypeMetadataCache.SyncHostFields(instance.Script, instance.Host);
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(
                $"ScriptRuntime: apply fields for '{instance.Script.GetType().FullName}' failed. {exception}");
            DestroyScript(instance);
        }
    }

    //第三阶段：统一附着并发送初始活动通知，同一实例只附着一次。
    private static void AttachHost(ScriptInstance instance)
    {
        if (instance.Destroyed || instance.Attached) return;
        instance.Attached = true;
        if (instance.Lifecycle != null)
        {
            try { instance.Lifecycle.OnComponentAttached(); }
            catch (Exception exception) { LogFailure(instance, "OnComponentAttached", exception); }
        }
        SendActive(instance, IsRunnable(instance));
    }

    //一次完整的宿主接管：构造、恢复字段、附着通知。
    private static ScriptInstance? CreateAndAttach(IntPtr host)
    {
        ScriptInstance? instance = ConstructHost(host);
        if (instance == null || instance.Attached) return instance;
        ApplyHostState(instance);
        AttachHost(instance);
        return instance.Destroyed ? null : instance;
    }

    //把全部活脚本的运行时值刷回原生宿主字段表。
    //原生字段表是保存、复制、Prefab、进入 Play 与程序集重载使用的快照。
    public static void FlushHostFields()
    {
        foreach (ScriptInstance instance in scripts.ToArray())
        {
            if (instance.Destroyed) continue;
            ManagedTypeMetadataCache.FlushHostFields(instance.Script, instance.Host);
        }
    }

    //字段事务结束后的通知入口。
    private static void OnHostFieldsChanged(IntPtr host)
    {
        if (scriptsByHost.TryGetValue(host, out ScriptInstance? instance)) NotifyFieldsChanged(instance);
    }

    //通知扩展字段已整体应用；未附着实例不通知。
    private static void NotifyFieldsChanged(ScriptInstance instance)
    {
        if (!instance.Attached || instance.Destroyed || instance.Lifecycle == null) return;
        try { instance.Lifecycle.OnComponentFieldsChanged(); }
        catch (Exception exception) { LogFailure(instance, "OnComponentFieldsChanged", exception); }
    }

    //把活动条件发送给扩展接口；未附着或状态未变时不通知。
    private static void SendActive(ScriptInstance instance, bool active)
    {
        if (!instance.Attached || instance.Lifecycle == null
            || instance.HasActiveState && instance.LastActive == active) return;
        instance.LastActive = active;
        instance.HasActiveState = true;
        try { instance.Lifecycle.OnComponentActiveChanged(active); }
        catch (Exception exception) { LogFailure(instance, "OnComponentActiveChanged", exception); }
    }

    //注册宿主、Wrapper、ObjectId 和 Ens 索引。
    internal static Script? GetOrCreateHost(IntPtr host) => CreateAndAttach(host)?.Script;

    private static void Register(ScriptInstance instance)
    {
        scripts.Add(instance);
        scriptsByHost.Add(instance.Host, instance);
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
        if (type == null || type.IsAbstract || !NativeBindingRuntime.IsManagedScript(type))
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
             current != null && current != typeof(Script);
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
            foreach (IntPtr host in Script.GetManagedHosts()) order[host] = order.Count;
            scripts.Sort((left, right) => order.GetValueOrDefault(left.Host, int.MaxValue)
                .CompareTo(order.GetValueOrDefault(right.Host, int.MaxValue)));
            updates.Clear();
            fixedUpdates.Clear();
            lateUpdates.Clear();
            guiCalls.Clear();
            foreach (ScriptInstance instance in scripts.ToArray())
            {
                if (!IsRunnable(instance)) continue;
                //编辑模式不执行 OnStart：界面上的组件只做结构同步与渲染。
                if (!instance.Started)
                {
                    instance.Started = true;
                    if (!SkipsGameLifecycle) Invoke(instance, instance.Start, "OnStart");
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
    //已附着实例先收到活动取消再收到分离，两者各至多一次；扩展异常不阻止释放。
    private static void DestroyScript(ScriptInstance instance)
    {
        if (instance.Destroyed) return;
        instance.Destroyed = true;
        //顺序固定为取消活动、结束游戏生命周期、分离扩展，各自至多一次。
        if (instance.Attached) SendActive(instance, false);
        if (instance.Started) Invoke(instance, instance.End, "OnEnd");
        instance.Started = false;
        if (instance.Attached)
        {
            if (instance.Lifecycle != null)
            {
                try { instance.Lifecycle.OnComponentDetached(); }
                catch (Exception exception) { LogFailure(instance, "OnComponentDetached", exception); }
            }
            instance.Attached = false;
        }
        instance.Lifecycle = null;
        scripts.Remove(instance);
        scriptsByHost.Remove(instance.Host);
        EnsId ens = instance.Script.EnsId;
        if (scriptsByEns.TryGetValue(ens, out List<ScriptInstance>? values))
        {
            values.Remove(instance);
            if (values.Count == 0) scriptsByEns.Remove(ens);
        }
        ScriptRuntimeRegistry.Unregister(instance.Script);
        instance.Script.DisconnectNative();
        callsDirty = true;
    }

    //应用上一阶段产生的结构变化。
    private static void PreparePhase()
    {
        if (dispatchDepth != 0) return;
        if (pendingAdds.Count != 0)
        {
            //同批新增宿主按三阶段一起处理，批内互相引用也能解析。
            IntPtr[] changes = [.. pendingAdds];
            foreach (IntPtr host in changes) pendingAdds.Remove(host);
            List<ScriptInstance> constructed = [];
            foreach (IntPtr host in changes)
            {
                ScriptInstance? instance = ConstructHost(host);
                if (instance != null && !instance.Attached) constructed.Add(instance);
            }
            foreach (ScriptInstance instance in constructed) ApplyHostState(instance);
            foreach (ScriptInstance instance in constructed) AttachHost(instance);
        }
        RebuildCalls();
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
        scriptsByEns.Clear();
        updates.Clear();
        fixedUpdates.Clear();
        lateUpdates.Clear();
        guiCalls.Clear();
        callsDirty = false;
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
        //只查当前会话与核心程序集，禁止扫描历史 AppDomain 程序集：
        //上一次会话残留的程序集会给出属于旧加载上下文的 Type，注册进绑定表后永远卸载不掉。
        Type? type = ManagedAssemblySession.GetGameAssembly()?.GetType(typeName, throwOnError: false);
        if (type != null) return type;
        type = ManagedAssemblySession.GetEditorAssembly()?.GetType(typeName, throwOnError: false);
        return type ?? typeof(ScriptRuntime).Assembly.GetType(typeName, throwOnError: false);
    }
}
