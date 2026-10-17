using System.Reflection;
using Orbeden;

namespace OrbedenEditor;

/// <summary>管理自定义编辑器注册、实例缓存和三类绘制回调。</summary>
internal static class CustomEditorRegistry
{
    private sealed record Registration(Type Component, Type Editor, bool Children, bool HasSceneGui, bool HasDrawGizmos)
    {
        internal readonly byte[] ProfileName = InteropText.EncodeUtf8("Editor/ComponentEditors/" + Editor.Name);
    }
    private sealed class Instance
    {
        internal ComponentEditor Editor = null!;
        internal readonly HashSet<string> FailedCallbacks = [];
    }
    private static readonly List<Registration> registrations = [];
    private static readonly Dictionary<string, Type> scripts = new(StringComparer.Ordinal);
    private static readonly Dictionary<string, Instance> instances = new(StringComparer.Ordinal);
    //按组件类型缓存编辑器匹配结果
    private static readonly Dictionary<(bool IsManaged, string TypeName), Registration?> registrationMatches = [];
    private static uint registryGeneration;
    //按程序集去重，重复注册同一个程序集不会产生重复项
    private static readonly HashSet<Assembly> registeredAssemblies = [];

    /// <summary>注册编辑器自带的内置编辑器；没有游戏程序集时也必须可用。</summary>
    internal static void RegisterBuiltins()
    {
        Register(typeof(ParticleSystemEditor).Assembly);
        //UI 组件住在核心程序集，注册它才能在按类型名解析时查到组件与它的 CustomEditor。
        Register(typeof(Script).Assembly);
    }

    /// <summary>注册程序集中的组件编辑器，拒绝重复和无效声明。</summary>
    internal static void Register(Assembly assembly)
    {
        if (!registeredAssemblies.Add(assembly)) return;
        registrationMatches.Clear();
        Type[] types;
        try { types = assembly.GetTypes(); }
        catch (ReflectionTypeLoadException exception) { types = exception.Types.OfType<Type>().ToArray(); }
        foreach (Type type in types)
            if (typeof(Component).IsAssignableFrom(type)) scripts[type.FullName ?? type.Name] = type;
        foreach (Type editor in types.OrderBy(type => type.FullName, StringComparer.Ordinal))
        foreach (CustomEditorAttribute attribute in editor.GetCustomAttributes<CustomEditorAttribute>(false))
        {
            if (editor.IsAbstract || editor.ContainsGenericParameters || !typeof(ComponentEditor).IsAssignableFrom(editor)
                || editor.GetConstructor(Type.EmptyTypes) == null || !typeof(Component).IsAssignableFrom(attribute.ComponentType))
            { Console.Error.WriteLine($"Invalid CustomEditor: {editor.FullName}"); continue; }
            if (registrations.Any(entry => entry.Component == attribute.ComponentType))
            { Console.Error.WriteLine($"Duplicate CustomEditor for {attribute.ComponentType.FullName}: {editor.FullName}"); continue; }
            bool hasSceneGui = editor.GetMethod(nameof(ComponentEditor.OnSceneGui), Type.EmptyTypes)!.DeclaringType != typeof(ComponentEditor);
            bool hasDrawGizmos = editor.GetMethod(nameof(ComponentEditor.OnDrawGizmos), Type.EmptyTypes)!.DeclaringType != typeof(ComponentEditor);
            registrations.Add(new(attribute.ComponentType, editor, attribute.EditorForChildClasses, hasSceneGui, hasDrawGizmos));
        }
    }

    /// <summary>卸载前释放全部类型和委托，避免阻止可收集程序集回收。</summary>
    internal static void Clear()
    {
        instances.Clear();
        registrationMatches.Clear();
        registryGeneration = 0;
        scripts.Clear();
        registrations.Clear();
        registeredAssemblies.Clear();
    }

    /// <summary>查找并缓存组件最近的编辑器注册。</summary>
    private static Registration? FindRegistration(ComponentEditorTarget target)
    {
        uint generation = EditorNativeComponents.RegistryGeneration;
        if (registryGeneration != generation)
        {
            registrationMatches.Clear();
            registryGeneration = generation;
        }
        var key = (target.IsManaged, target.TypeName);
        if (registrationMatches.TryGetValue(key, out Registration? cached)) return cached;

        Type? component = target.IsManaged ? scripts.GetValueOrDefault(target.TypeName) : null;
        Registration? registration = null;
        for (Type? current = component; current != null; current = current.BaseType)
        {
            registration = registrations.FirstOrDefault(entry => (entry.Component == current
                || entry.Component.FullName == current.FullName && entry.Component.Assembly.GetName().Name == current.Assembly.GetName().Name)
                && (current == component || entry.Children));
            if (registration != null) break;
        }
        //原生类型按真实反射继承链匹配，不依赖游戏包装的模块初始化器是否已执行。
        if (!target.IsManaged)
        {
            int bestDepth = -1;
            foreach (Registration candidate in registrations)
            {
                if (NativeBindingRuntime.IsManagedScript(candidate.Component)) continue;
                string name = NativeBindingRuntime.GetNativeTypeName(candidate.Component) ?? candidate.Component.Name;
                if (name == target.TypeName) { registration = candidate; break; }
                if (!candidate.Children || !EditorNativeComponents.MatchesComponentType(target.ObjectId, name)) continue;
                int depth = 0;
                for (Type? current = candidate.Component; current != null; current = current.BaseType) ++depth;
                if (depth > bestDepth) { bestDepth = depth; registration = candidate; }
            }
        }
        registrationMatches[key] = registration;
        return registration;
    }

    /// <summary>获取或创建目标对应的编辑器实例。</summary>
    private static Instance? Resolve(IReadOnlyList<ComponentEditorTarget> targets)
    {
        Registration? registration = FindRegistration(targets[0]);
        if (registration == null) return null;
        string key = registration.Editor.FullName + ":" + string.Join(',', targets.Select(target => target.ObjectId));
        if (instances.TryGetValue(key, out Instance? found)) return found;
        try
        {
            ComponentEditor editor = (ComponentEditor)Activator.CreateInstance(registration.Editor)!;
            editor.Targets = targets.ToArray();
            editor.Properties = new PropertyDocument(targets.Select(target => (IPropertyTarget)new NativeComponentPropertyTarget(
                new(target.ObjectId, target.TypeName, target.IsManaged), EditorApplication.MarkWorldDirty)).ToArray());
            return instances[key] = new Instance { Editor = editor };
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"Cannot create CustomEditor {registration.Editor.FullName}: {exception}");
            registrations.Remove(registration);
            registrationMatches.Clear();
            return null;
        }
    }

    /// <summary>隔离单个编辑器异常；失败回调停用到下次重载，其余编辑器继续运行。</summary>
    private static bool Invoke(Instance instance, string callback, Action draw)
    {
        if (instance.FailedCallbacks.Contains(callback)) return false;
        bool previousRectEditor = EditorSceneHandles.IsRectComponentEditor;
        EditorSceneHandles.IsRectComponentEditor = callback == nameof(ComponentEditor.OnSceneGui)
            && instance.Editor.Target.TypeName == "Orbeden.UILayout";
        EditorGUI.PushId(callback + ":" + string.Join(',', instance.Editor.Targets.Select(target => target.ObjectId)));
        try
        {
            ReadOnlySpan<byte> callbackProfile = callback switch
            {
                nameof(ComponentEditor.OnDrawGizmos) => "Editor/ComponentEditors/OnDrawGizmos"u8,
                nameof(ComponentEditor.OnSceneGui) => "Editor/ComponentEditors/OnSceneGui"u8,
                _ => default,
            };
            using (new EditorProfileScope(callbackProfile)) draw();
            if (instance.Editor.Properties.HasPendingChanges)
            {
                using EditorProfileScope applyProfile = new(callbackProfile.IsEmpty ? default : "Editor/ComponentEditors/ApplyChanges"u8);
                if (!instance.Editor.Properties.ApplyChanges($"Edit {instance.Editor.Target.TypeName}"))
                    throw new InvalidOperationException("CustomEditor property write failed.");
            }
            return true;
        }
        catch (Exception exception)
        {
            instance.FailedCallbacks.Add(callback);
            instance.Editor.Properties.Update();
            Console.Error.WriteLine($"{instance.Editor.GetType().FullName}.{callback}: {exception}");
            return false;
        }
        finally
        {
            EditorSceneHandles.IsRectComponentEditor = previousRectEditor;
            EditorGUI.PopId();
        }
    }

    /// <summary>绘制自定义 Inspector，未注册或失败时由调用方绘制默认界面。</summary>
    internal static bool DrawInspector(IReadOnlyList<ComponentEditorTarget> targets, PropertyDocument document, Action drawDefault)
    {
        Instance? instance = Resolve(targets);
        if (instance == null) return false;
        instance.Editor.Properties = document;
        instance.Editor.IsSelected = true;
        instance.Editor.DefaultInspector = drawDefault;
        try { return Invoke(instance, nameof(ComponentEditor.OnDrawInspector), instance.Editor.OnDrawInspector); }
        finally { instance.Editor.DefaultInspector = null; }
    }

    /// <summary>独立于 Inspector 可见性派发场景回调，清理已销毁目标。</summary>
    internal static void DrawScene()
    {
        if (registrations.Count == 0) return;
        bool gizmosVisible = Gizmos.Visible;
        HashSet<int> alive = [];
        EnsId[] worldEns;
        using (new EditorProfileScope("Editor/ComponentEditors/EnumerateEns"u8))
            worldEns = EditorNativeComponents.GetWorldEns();
        foreach (EnsId ens in worldEns)
        {
            bool selected, active;
            using (new EditorProfileScope("Editor/ComponentEditors/ReadEnsState"u8))
            {
                selected = Gizmos.IsSelected(ens);
                active = Ens.FromId(ens).WorldActive;
            }
            List<NativeComponentInfo> components;
            using (new EditorProfileScope("Editor/ComponentEditors/EnumerateComponents"u8))
                components = EditorNativeComponents.GetComponents(ens);
            foreach (NativeComponentInfo info in components)
            {
                alive.Add(info.ObjectId);
                if (!active && !selected) continue;
                if (!selected && !gizmosVisible) continue;

                //筛选本帧实际需要执行的场景回调
                ComponentEditorTarget target = new(info.ObjectId, ens, info.TypeName, info.IsManaged);
                Registration? registration;
                using (new EditorProfileScope("Editor/ComponentEditors/MatchRegistration"u8))
                    registration = FindRegistration(target);
                if (registration == null) continue;
                bool drawGizmos = gizmosVisible && registration.HasDrawGizmos;
                bool drawSceneGui = selected && registration.HasSceneGui;
                if (!drawGizmos && !drawSceneGui) continue;

                using EditorProfileScope editorProfile = new(registration.ProfileName);
                Instance? instance;
                using (new EditorProfileScope("Editor/ComponentEditors/ResolveInstance"u8))
                    instance = Resolve([target]);
                if (instance == null) continue;
                drawGizmos &= !instance.FailedCallbacks.Contains(nameof(ComponentEditor.OnDrawGizmos));
                drawSceneGui &= !instance.FailedCallbacks.Contains(nameof(ComponentEditor.OnSceneGui));
                if (!drawGizmos && !drawSceneGui) continue;
                instance.Editor.IsSelected = selected;
                using (new EditorProfileScope("Editor/ComponentEditors/RefreshProperties"u8))
                    instance.Editor.Properties.Update();
                if (drawGizmos) Invoke(instance, nameof(ComponentEditor.OnDrawGizmos), instance.Editor.OnDrawGizmos);
                if (drawSceneGui) Invoke(instance, nameof(ComponentEditor.OnSceneGui), instance.Editor.OnSceneGui);
            }
        }
        using (new EditorProfileScope("Editor/ComponentEditors/Cleanup"u8))
            foreach (string key in instances.Where(pair => pair.Value.Editor.Targets.Any(target => !alive.Contains(target.ObjectId))).Select(pair => pair.Key).ToArray())
                instances.Remove(key);
    }
}
