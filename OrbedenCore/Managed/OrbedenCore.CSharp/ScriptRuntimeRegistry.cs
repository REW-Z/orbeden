using System.Collections.Generic;

namespace OrbedenCore.CSharp;

/// <summary>记录当前托管运行态脚本实例，供 Editor CLR Inspector 查询。</summary>
public static class ScriptRuntimeRegistry
{
    private static readonly Dictionary<EnsId, List<ScriptBehaviour>> scriptsByEns = [];

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
    }

    /// <summary>清空所有运行态脚本实例记录。</summary>
    public static void Clear()
    {
        scriptsByEns.Clear();
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
