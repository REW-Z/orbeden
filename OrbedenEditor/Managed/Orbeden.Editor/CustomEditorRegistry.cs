using System.Reflection;
using Orbeden;

namespace OrbedenEditor;

/// <summary>管理自定义编辑器注册、实例缓存和三类绘制回调。</summary>
internal static class CustomEditorRegistry
{
    private sealed record Registration(Type Component, Type Editor, bool Children);
    private sealed class Instance
    {
        internal ComponentEditor Editor = null!;
        internal readonly HashSet<string> FailedCallbacks = [];
    }
    private static readonly List<Registration> registrations = [];
    private static readonly Dictionary<string, Type> scripts = new(StringComparer.Ordinal);
    private static readonly Dictionary<string, Instance> instances = new(StringComparer.Ordinal);

    /// <summary>注册程序集中的组件编辑器，拒绝重复和无效声明。</summary>
    internal static void Register(Assembly assembly)
    {
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
            registrations.Add(new(attribute.ComponentType, editor, attribute.EditorForChildClasses));
        }
    }

    /// <summary>卸载前释放全部类型和委托，避免阻止可收集程序集回收。</summary>
    internal static void Clear()
    {
        instances.Clear();
        scripts.Clear();
        registrations.Clear();
    }

    /// <summary>按组件最近的注册基类选择编辑器。</summary>
    private static Instance? Resolve(IReadOnlyList<ComponentEditorTarget> targets)
    {
        ComponentEditorTarget primary = targets[0];
        Type? component = primary.IsManaged ? scripts.GetValueOrDefault(primary.TypeName) : null;
        Registration? registration = null;
        for (Type? current = component; current != null; current = current.BaseType)
        {
            registration = registrations.FirstOrDefault(entry => (entry.Component == current
                || entry.Component.FullName == current.FullName && entry.Component.Assembly.GetName().Name == current.Assembly.GetName().Name)
                && (current == component || entry.Children));
            if (registration != null) break;
        }
        //原生类型按真实反射继承链匹配，不依赖游戏包装的模块初始化器是否已执行。
        if (!primary.IsManaged)
        {
            int bestDepth = -1;
            foreach (Registration candidate in registrations)
            {
                if (NativeBindingRuntime.IsManagedScript(candidate.Component)) continue;
                string name = NativeBindingRuntime.GetNativeTypeName(candidate.Component) ?? candidate.Component.Name;
                if (name == primary.TypeName) { registration = candidate; break; }
                if (!candidate.Children || !EditorNativeComponents.MatchesComponentType(primary.ObjectId, name)) continue;
                int depth = 0;
                for (Type? current = candidate.Component; current != null; current = current.BaseType) ++depth;
                if (depth > bestDepth) { bestDepth = depth; registration = candidate; }
            }
        }
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
            return null;
        }
    }

    /// <summary>隔离单个编辑器异常；失败回调停用到下次重载，其余编辑器继续运行。</summary>
    private static bool Invoke(Instance instance, string callback, Action draw)
    {
        if (instance.FailedCallbacks.Contains(callback)) return false;
        EditorGUI.PushId(callback + ":" + string.Join(',', instance.Editor.Targets.Select(target => target.ObjectId)));
        try
        {
            draw();
            if (instance.Editor.Properties.HasPendingChanges && !instance.Editor.Properties.ApplyChanges($"Edit {instance.Editor.Target.TypeName}"))
                throw new InvalidOperationException("CustomEditor property write failed.");
            return true;
        }
        catch (Exception exception)
        {
            instance.FailedCallbacks.Add(callback);
            instance.Editor.Properties.Update();
            Console.Error.WriteLine($"{instance.Editor.GetType().FullName}.{callback}: {exception}");
            return false;
        }
        finally { EditorGUI.PopId(); }
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
        HashSet<int> alive = [];
        foreach (EnsId ens in EditorNativeComponents.GetWorldEns())
        {
            bool selected = Gizmos.IsSelected(ens);
            bool active = Ens.FromId(ens).WorldActive;
            foreach (NativeComponentInfo info in EditorNativeComponents.GetComponents(ens))
            {
                alive.Add(info.ObjectId);
                if (!active && !selected) continue;
                if (!selected && !Gizmos.Visible) continue;
                Instance? instance = Resolve([new(info.ObjectId, ens, info.TypeName, info.IsManaged)]);
                if (instance == null) continue;
                instance.Editor.IsSelected = selected;
                instance.Editor.Properties.Update();
                if (Gizmos.Visible) Invoke(instance, nameof(ComponentEditor.OnDrawGizmos), instance.Editor.OnDrawGizmos);
                if (selected) Invoke(instance, nameof(ComponentEditor.OnSceneGui), instance.Editor.OnSceneGui);
            }
        }
        foreach (string key in instances.Where(pair => pair.Value.Editor.Targets.Any(target => !alive.Contains(target.ObjectId))).Select(pair => pair.Key).ToArray())
            instances.Remove(key);
    }
}
