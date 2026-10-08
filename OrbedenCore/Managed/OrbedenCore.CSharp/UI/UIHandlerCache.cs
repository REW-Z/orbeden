using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 节点上的输入处理器缓存。按组件集合版本失效，不逐事件扫描程序集；
/// 组件启停不改表，派发时按当前启用状态逐个跳过。
/// </summary>
internal sealed class UIHandlerCache
{
    private sealed class Entry
    {
        internal ulong Revision;
        internal Script[] Handlers = [];
    }

    private readonly Dictionary<EnsId, Entry> entries = [];

    /// <summary>按版本取得组件快照，顺序与挂载顺序一致。</summary>
    internal Script[] GetHandlers(EnsId ens)
    {
        ulong revision = ScriptRuntimeRegistry.GetScriptRevision(ens);
        if (revision == 0)
        {
            entries.Remove(ens);
            return [];
        }
        if (entries.TryGetValue(ens, out Entry? entry) && entry.Revision == revision) return entry.Handlers;

        entry ??= new Entry();
        entry.Revision = revision;
        IReadOnlyList<Script> scripts = ScriptRuntimeRegistry.GetScripts(ens);
        entry.Handlers = new Script[scripts.Count];
        for (int index = 0; index < scripts.Count; ++index) entry.Handlers[index] = scripts[index];
        entries[ens] = entry;
        return entry.Handlers;
    }

    /// <summary>换世界时清空；节点身份不跨世界复用。</summary>
    internal void Clear() => entries.Clear();

    /// <summary>移除离开 UI 树的节点引用。</summary>
    internal void Remove(EnsId ens) => entries.Remove(ens);
}
