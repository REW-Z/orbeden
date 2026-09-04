using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;

namespace Orbeden;

/// <summary>记录当前托管运行态脚本实例，供 Editor CLR Inspector 查询。</summary>
public static class ScriptRuntimeRegistry
{
    private static readonly Dictionary<EnsId, List<ScriptBehaviour>> scriptsByEns = [];
    private static readonly Dictionary<ScriptBehaviour, ulong> slotsByScript = [];
    private static readonly Dictionary<ulong, ScriptBehaviour> scriptsBySlot = [];
    private static uint generation = 1;

    /// <summary>注册脚本实例。</summary>
    internal static void Register(ScriptBehaviour script)
    {
        if (script.EnsId.IsNull) return;

        if (!scriptsByEns.TryGetValue(script.EnsId, out List<ScriptBehaviour>? scripts))
        {
            scripts = [];
            scriptsByEns.Add(script.EnsId, scripts);
        }

        if (!scripts.Contains(script))
        {
            scripts.Add(script);
        }
        if (!slotsByScript.ContainsKey(script))
        {
            ulong slot = unchecked((uint)script.InstanceId);
            if (slot == 0 || scriptsBySlot.ContainsKey(slot)) return;
            slotsByScript.Add(script, slot);
            scriptsBySlot.Add(slot, script);
        }
    }

    /// <summary>注销脚本实例。</summary>
    internal static void Unregister(ScriptBehaviour script)
    {
        if (script.EnsId.IsNull) return;
        if (!scriptsByEns.TryGetValue(script.EnsId, out List<ScriptBehaviour>? scripts)) return;

        scripts.Remove(script);
        if (scripts.Count == 0)
        {
            scriptsByEns.Remove(script.EnsId);
        }
        if (slotsByScript.Remove(script, out ulong slot)) scriptsBySlot.Remove(slot);
    }

    /// <summary>清空所有运行态脚本实例记录。</summary>
    public static void Clear()
    {
        scriptsByEns.Clear();
        slotsByScript.Clear();
        scriptsBySlot.Clear();
        ++generation;
        if (generation == 0) generation = 1;
    }

    internal static bool TryGetHandle(ScriptBehaviour script, out ComponentHandle handle)
    {
        if (slotsByScript.TryGetValue(script, out ulong slot))
        {
            handle = new ComponentHandle(ComponentDomain.Managed, generation, slot);
            return true;
        }
        handle = default;
        return false;
    }

    internal static bool TryResolve(ComponentHandle handle, [NotNullWhen(true)] out ScriptBehaviour? script)
    {
        if (handle.Domain == ComponentDomain.Managed && handle.Generation == generation && scriptsBySlot.TryGetValue(handle.Slot, out script)) return true;
        script = null;
        return false;
    }

    /// <summary>创建具有独立原生组件身份的 C# 脚本。</summary>
    public static T? AddScript<T>(EnsId ens) where T : ScriptBehaviour
    {
        return ScriptRuntime.AddManagedScript(ens, typeof(T)) as T;
    }

    /// <summary>移除 C# 脚本及其原生宿主；已启动实例只执行一次 End。</summary>
    public static bool RemoveScript(ScriptBehaviour script)
    {
        return ScriptRuntime.RemoveManagedScript(script);
    }

    /// <summary>获取指定 Ens 上的运行态脚本实例。</summary>
    public static IReadOnlyList<ScriptBehaviour> GetScripts(EnsId ens)
    {
        return scriptsByEns.TryGetValue(ens, out List<ScriptBehaviour>? scripts) ? scripts : [];
    }

    /// <summary>获取当前所有运行态脚本实例快照。</summary>
    public static IReadOnlyList<ScriptBehaviour> GetAllScripts()
    {
        List<ScriptBehaviour> result = [];
        foreach (List<ScriptBehaviour> scripts in scriptsByEns.Values)
        {
            result.AddRange(scripts);
        }

        return result;
    }
}
