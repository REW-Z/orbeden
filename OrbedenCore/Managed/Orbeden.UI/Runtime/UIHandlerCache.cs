using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 节点上的输入处理器缓存。按节点的托管组件数量失效，不逐事件扫描程序集；
/// 组件启停不改表，派发时按当前启用状态逐个跳过。
/// </summary>
internal sealed class UIHandlerCache
{
    private sealed class Entry
    {
        internal int ScriptCount = -1;
        internal Script[] Handlers = [];
    }

    private readonly Dictionary<EnsId, Entry> entries = [];

    /// <summary>取节点上的组件，顺序与挂载顺序一致；数量没变的节点直接命中缓存。</summary>
    internal Script[] GetHandlers(EnsId ens)
    {
        int count = ScriptRuntimeRegistry.GetScriptCount(ens);
        if (entries.TryGetValue(ens, out Entry? entry) && entry.ScriptCount == count) return entry.Handlers;

        entry ??= new Entry();
        entry.ScriptCount = count;
        IReadOnlyList<Script> scripts = ScriptRuntimeRegistry.GetScripts(ens);
        entry.Handlers = new Script[scripts.Count];
        for (int index = 0; index < scripts.Count; ++index) entry.Handlers[index] = scripts[index];
        entries[ens] = entry;
        return entry.Handlers;
    }

    /// <summary>换世界时清空；节点身份不跨世界复用。</summary>
    internal void Clear() => entries.Clear();
}
