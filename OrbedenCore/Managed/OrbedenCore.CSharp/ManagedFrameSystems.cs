using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 托管帧系统的注册表。登记的是工厂，不创建任何世界对象；
/// 世界附着时才按 id 的 ordinal 升序构造实例，分离时逆序释放。
/// </summary>
public static class ManagedFrameSystems
{
    private static readonly Dictionary<string, Func<IManagedFrameSystem>> factories = new(StringComparer.Ordinal);
    private static readonly List<KeyValuePair<string, IManagedFrameSystem>> active = [];
    private static ulong worldRevision;

    /// <summary>当前是否已经附着世界；未附着时注册表不构造实例。</summary>
    public static bool HasActiveWorld => active.Count != 0;

    /// <summary>判断一个 id 是否已经登记；模块初始化用它避免重复登记。</summary>
    public static bool IsRegistered(string id) => !string.IsNullOrEmpty(id) && factories.ContainsKey(id);

    /// <summary>登记帧系统工厂；重复 id 立即报错。</summary>
    public static void Register(string id, Func<IManagedFrameSystem> factory)
    {
        ArgumentException.ThrowIfNullOrEmpty(id);
        ArgumentNullException.ThrowIfNull(factory);
        if (factories.ContainsKey(id))
            throw new InvalidOperationException($"Managed frame system '{id}' is already registered.");
        factories.Add(id, factory);
    }

    /// <summary>注销帧系统工厂；只允许在世界分离后执行，重复注销无副作用。</summary>
    public static void Unregister(string id)
    {
        if (string.IsNullOrEmpty(id) || !factories.Remove(id)) return;
    }

    /// <summary>按 id 升序构造全部帧系统并附着世界。</summary>
    internal static void AttachWorld(ulong revision, bool editorMode)
    {
        DetachWorld();
        worldRevision = revision == 0 ? 1 : revision;
        List<string> order = [.. factories.Keys];
        order.Sort(StringComparer.Ordinal);
        foreach (string id in order)
        {
            IManagedFrameSystem system;
            try { system = factories[id](); }
            catch (Exception exception)
            {
                Console.Error.WriteLine($"ManagedFrameSystems: create '{id}' failed. {exception}");
                continue;
            }
            try { system.AttachWorld(worldRevision, editorMode); }
            catch (Exception exception)
            {
                Console.Error.WriteLine($"ManagedFrameSystems: attach '{id}' failed. {exception}");
                continue;
            }
            active.Add(new KeyValuePair<string, IManagedFrameSystem>(id, system));
        }
    }

    /// <summary>按附着顺序逆序分离并释放全部帧系统。</summary>
    internal static void DetachWorld()
    {
        for (int index = active.Count - 1; index >= 0; --index)
        {
            try { active[index].Value.DetachWorld(); }
            catch (Exception exception)
            {
                Console.Error.WriteLine(
                    $"ManagedFrameSystems: detach '{active[index].Key}' failed. {exception}");
            }
        }
        active.Clear();
    }

    /// <summary>驱动输入阶段；单个系统异常不阻断其他系统。</summary>
    internal static void ProcessInput(float deltaTime)
    {
        for (int index = 0; index < active.Count; ++index)
        {
            try { active[index].Value.ProcessInput(deltaTime); }
            catch (Exception exception) { LogPhaseFailure(active[index].Key, "ProcessInput", exception); }
        }
    }

    /// <summary>驱动渲染准备阶段；单个系统异常不阻断其他系统。</summary>
    internal static void PrepareRender(float deltaTime)
    {
        for (int index = 0; index < active.Count; ++index)
        {
            try { active[index].Value.PrepareRender(deltaTime); }
            catch (Exception exception) { LogPhaseFailure(active[index].Key, "PrepareRender", exception); }
        }
    }

    /// <summary>程序集卸载前清空注册表；实例此时应已随世界分离释放。</summary>
    internal static void Clear()
    {
        DetachWorld();
        factories.Clear();
        worldRevision = 0;
    }

    private static void LogPhaseFailure(string id, string phase, Exception exception) =>
        Console.Error.WriteLine($"ManagedFrameSystems: '{id}' failed in {phase}. {exception}");
}
