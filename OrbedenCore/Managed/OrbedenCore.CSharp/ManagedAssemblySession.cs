using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;

namespace Orbeden;

/// <summary>
/// 引擎唯一的托管程序集会话。整个进程只持有一个可回收的 AssemblyLoadContext，
/// Inspector、CustomEditor 与运行时因此共用同一份游戏 Type 实例，不会各自加载一份。
/// OrbedenCore.CSharp 与传入的编辑器合同程序集直接共享，其余依赖按简单程序集名从游戏输出解析。
/// </summary>
public static class ManagedAssemblySession
{
    private sealed class SessionLoadContext(string assemblyPath) : AssemblyLoadContext(isCollectible: true)
    {
        private readonly AssemblyDependencyResolver resolver = new(assemblyPath);
        private readonly List<string> sharedNames = [];
        private readonly List<Assembly> sharedAssemblies = [];

        //登记必须直接共享的程序集：核心程序集与编辑器合同。
        internal void Share(Assembly assembly)
        {
            string name = assembly.GetName().Name ?? string.Empty;
            if (sharedNames.Contains(name)) return;
            sharedNames.Add(name);
            sharedAssemblies.Add(assembly);
        }

        //从流加载，不锁定构建输出文件。
        [UnconditionalSuppressMessage("Trimming", "IL2026",
            Justification = "只有不做裁剪的 Editor CLR 运行时才会动态加载文件。")]
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
            for (int index = 0; index < sharedNames.Count; ++index)
                if (name.Name == sharedNames[index]) return sharedAssemblies[index];
            string? path = resolver.ResolveAssemblyToPath(name);
            return path == null ? null : LoadFile(path);
        }
    }

    private static readonly List<Action> unloadHandlers = [];
    private static SessionLoadContext? context;
    private static Assembly? gameAssembly;
    private static Assembly? editorAssembly;
    private static string loadedPath = string.Empty;
    private static ulong generation = 1;

    /// <summary>当前会话加载的游戏程序集；未加载时为空。</summary>
    public static Assembly? GetGameAssembly() => gameAssembly;

    /// <summary>随游戏程序集一起加载的 &lt;Game&gt;.Editor 程序集；不存在时为空。</summary>
    public static Assembly? GetEditorAssembly() => editorAssembly;

    /// <summary>会话代次；每次卸载都会推进，用于判断缓存的 Type 是否还有效。</summary>
    public static ulong GetGeneration() => generation;

    /// <summary>登记会话卸载前的清理动作；编辑器用它注销菜单、CustomEditor 等静态表。</summary>
    public static void RegisterUnloadHandler(Action handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        if (!unloadHandlers.Contains(handler)) unloadHandlers.Add(handler);
    }

    /// <summary>注销会话卸载清理动作。</summary>
    public static void UnregisterUnloadHandler(Action handler)
    {
        unloadHandlers.Remove(handler);
    }

    /// <summary>
    /// 加载游戏程序集。同一路径重复调用只返回成功，不重复登记模块；
    /// 路径变化时要求世界已经分离，否则拒绝替换会话，调用方应先 Unload。
    /// editorContract 传编辑器合同程序集（Orbeden.Editor），Player 侧传空。
    /// </summary>
    public static bool Load(string gameAssemblyPath, Assembly? editorContract)
    {
        if (string.IsNullOrWhiteSpace(gameAssemblyPath) || !File.Exists(gameAssemblyPath)) return false;
        string path = Path.GetFullPath(gameAssemblyPath);
        if (context != null)
        {
            if (gameAssembly != null && string.Equals(path, loadedPath, StringComparison.OrdinalIgnoreCase))
                return true;
            //世界还没分离时不允许替换会话，避免已登记的包装指向旧程序集的类型。
            if (ScriptRuntime.HasAttachedWorld) return false;
            Unload();
        }

        SessionLoadContext created = new(path);
        try
        {
            created.Share(typeof(Script).Assembly);
            if (editorContract != null) created.Share(editorContract);

            Assembly loaded = created.LoadFile(path);
            //模块初始化必须早于任何组件创建，否则静态字段还没就绪。
            RuntimeHelpers.RunModuleConstructor(loaded.ManifestModule.ModuleHandle);

            string editorPath = Path.Combine(Path.GetDirectoryName(path)!,
                Path.GetFileNameWithoutExtension(path) + ".Editor.dll");
            Assembly? editor = File.Exists(editorPath) ? created.LoadFile(editorPath) : null;
            if (editor != null) RuntimeHelpers.RunModuleConstructor(editor.ManifestModule.ModuleHandle);

            context = created;
            gameAssembly = loaded;
            editorAssembly = editor;
            loadedPath = path;
            NativeBindingRuntime.ActivateAssembly(loaded);
            if (editor != null) NativeBindingRuntime.ActivateAssembly(editor);
            return true;
        }
        catch (Exception exception)
        {
            //加载失败保留场景字段并显示错误；不尝试运行部分注册成功的程序集。
            Console.Error.WriteLine($"ManagedAssemblySession: load '{path}' failed. {exception}");
            try { created.Unload(); } catch { }
            return false;
        }
    }

    /// <summary>
    /// 卸载会话。顺序为：清理回调（停止帧、取消输入、断开包装、注销菜单与绑定缓存）、
    /// 清空脚本工厂与元数据、注销绑定类型，最后释放加载上下文。
    /// </summary>
    public static void Unload()
    {
        foreach (Action handler in unloadHandlers.ToArray())
        {
            try { handler(); }
            catch (Exception exception)
            {
                Console.Error.WriteLine($"ManagedAssemblySession: unload handler failed. {exception}");
            }
        }
        //清理动作属于本次会话：握有它们的程序集马上要被释放，留着会跨会话累积。
        unloadHandlers.Clear();
        ScriptRuntime.OnAssemblySessionUnloaded();
        if (gameAssembly != null) NativeBindingRuntime.UnregisterAssembly(gameAssembly);
        if (editorAssembly != null) NativeBindingRuntime.UnregisterAssembly(editorAssembly);
        gameAssembly = null;
        editorAssembly = null;
        loadedPath = string.Empty;
        context?.Unload();
        context = null;
        ++generation;
        if (generation == 0) generation = 1;
    }
}
