using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace Orbeden;

/// <summary>Runtime 托管入口，由 C++ ScriptSystem 调用。</summary>
public static unsafe class ScriptRuntime
{
    private static readonly List<Assembly> scriptAssemblies = [];
    private static readonly Dictionary<ulong, ScriptBehaviour> behaviours = [];
    private static ulong nextBehaviourHandle = 1;

    /// <summary>初始化 Runtime 托管桥接。</summary>
    [UnmanagedCallersOnly]
    public static void Initialize(IntPtr runtimeGuiApi, IntPtr ensBind, IntPtr spaceComponentBind, IntPtr staticMeshRendererBind)
    {
        NativeGui.Initialize(runtimeGuiApi);
        EnsBind.Initialize(ensBind);
        SpaceComponentBind.Initialize(spaceComponentBind);
        StaticMeshRendererBind.Initialize(staticMeshRendererBind);
    }

    /// <summary>加载脚本程序集。</summary>
    [UnmanagedCallersOnly]
    public static byte LoadScriptAssembly(byte* path, int length)
    {
        try
        {
            string assemblyPath = ReadUtf8(path, length);
            if (string.IsNullOrWhiteSpace(assemblyPath)) return 0;

            foreach (Assembly assembly in scriptAssemblies)
            {
                if (string.Equals(assembly.Location, assemblyPath, StringComparison.OrdinalIgnoreCase))
                {
                    return 1;
                }
            }

            scriptAssemblies.Add(AssemblyLoadContext.Default.LoadFromAssemblyPath(assemblyPath));
            return 1;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 0;
        }
    }

    /// <summary>创建托管脚本实例。</summary>
    [UnmanagedCallersOnly]
    public static ulong CreateBehaviour(byte* typeName, int length, EnsId ens)
    {
        try
        {
            string name = ReadUtf8(typeName, length);
            Type? type = FindScriptType(name);
            if (type == null || !typeof(ScriptBehaviour).IsAssignableFrom(type)) return 0;

            ScriptBehaviour? behaviour = Activator.CreateInstance(type) as ScriptBehaviour;
            if (behaviour == null) return 0;

            behaviour.Ens = new Ens(ens);
            ulong handle = nextBehaviourHandle++;
            if (handle == 0) handle = nextBehaviourHandle++;
            behaviours[handle] = behaviour;
            return handle;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 0;
        }
    }

    /// <summary>调用托管脚本启动回调。</summary>
    [UnmanagedCallersOnly]
    public static void StartBehaviour(ulong handle)
    {
        try
        {
            if (behaviours.TryGetValue(handle, out ScriptBehaviour? behaviour))
            {
                behaviour.InvokeStart();
            }
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
        }
    }

    /// <summary>调用托管脚本每帧回调。</summary>
    [UnmanagedCallersOnly]
    public static void UpdateBehaviour(ulong handle, float deltaTime)
    {
        try
        {
            if (behaviours.TryGetValue(handle, out ScriptBehaviour? behaviour))
            {
                behaviour.InvokeUpdate(deltaTime);
            }
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
        }
    }

    /// <summary>调用托管脚本结束回调并释放句柄。</summary>
    [UnmanagedCallersOnly]
    public static void EndBehaviour(ulong handle)
    {
        try
        {
            if (behaviours.Remove(handle, out ScriptBehaviour? behaviour))
            {
                behaviour.InvokeEnd();
            }
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
        }
    }

    //从 C++ 传入的 UTF-8 指针读取字符串
    private static string ReadUtf8(byte* text, int length)
    {
        if (text == null || length <= 0) return string.Empty;
        return Encoding.UTF8.GetString(text, length);
    }

    //按完整类型名查找脚本类型
    private static Type? FindScriptType(string typeName)
    {
        if (string.IsNullOrWhiteSpace(typeName)) return null;

        Type? type = Type.GetType(typeName, false);
        if (type != null) return type;

        foreach (Assembly assembly in scriptAssemblies)
        {
            type = assembly.GetType(typeName, false);
            if (type != null) return type;
        }

        foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            type = assembly.GetType(typeName, false);
            if (type != null) return type;
        }

        return null;
    }
}
