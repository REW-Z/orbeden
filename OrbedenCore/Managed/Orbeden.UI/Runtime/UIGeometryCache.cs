using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 图形网格的缓存。键为上下文代次 + 组件运行时 ID + 片段编号 + 视图变体：
/// 换了世界或程序集、组件被复用、片段结构变化或换了视图变体都会得到新的网格。
/// 内容变化推进 revision，没有变化的网格只在帧命令里引用 meshId，不重复上传。
/// </summary>
internal sealed class UIGeometryCache
{
    /// <summary>网格键。视图变体用于世界空间相机按像素字号分变体这类场合。</summary>
    internal readonly record struct MeshKey(ulong ContextGeneration, int ObjectId, int FragmentIndex, int ViewVariant);

    private sealed class MeshEntry
    {
        internal ulong MeshId;
        internal ulong Revision;
        internal ulong LastUsedFrame;
    }

    private readonly Dictionary<MeshKey, MeshEntry> entries = [];
    private readonly List<MeshKey> unused = [];
    private ulong nextMeshId = 1;
    private ulong generation;

    /// <summary>当前上下文代次；世界或会话更换时由帧构建器推进。</summary>
    internal ulong Generation => generation;

    /// <summary>
    /// 开始一个新上下文；旧代次的缓存全部作废。网格标识不在换代时回收：
    /// 原生侧的释放可能还没走完，复用标识会让迟到的释放命中新网格。
    /// </summary>
    internal void Reset(ulong contextGeneration)
    {
        generation = contextGeneration == 0 ? 1 : contextGeneration;
        entries.Clear();
        unused.Clear();
    }

    /// <summary>
    /// 取得网格标识。内容有变化或首次出现时返回真，调用方据此决定是否上传顶点。
    /// </summary>
    internal bool Acquire(MeshKey key, ulong revision, ulong frameId, out ulong meshId)
    {
        if (entries.TryGetValue(key, out MeshEntry? entry))
        {
            entry.LastUsedFrame = frameId;
            bool changed = entry.Revision != revision;
            entry.Revision = revision;
            meshId = entry.MeshId;
            return changed;
        }

        entry = new MeshEntry { MeshId = nextMeshId++, Revision = revision, LastUsedFrame = frameId };
        entries.Add(key, entry);
        meshId = entry.MeshId;
        return true;
    }

    /// <summary>收集本帧未被引用、且属于当前代次的网格，供原生侧释放。</summary>
    internal void CollectUnused(ulong frameId, List<ulong> removed)
    {
        unused.Clear();
        foreach ((MeshKey key, MeshEntry entry) in entries)
        {
            if (key.ContextGeneration == generation && entry.LastUsedFrame != frameId) unused.Add(key);
        }

        foreach (MeshKey key in unused)
        {
            removed.Add(entries[key].MeshId);
            entries.Remove(key);
        }
    }

    /// <summary>清空全部缓存；上下文销毁时调用。</summary>
    internal void Clear()
    {
        entries.Clear();
        unused.Clear();
        nextMeshId = 1;
    }
}
