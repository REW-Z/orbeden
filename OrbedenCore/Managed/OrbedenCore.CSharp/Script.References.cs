using System.Collections.Generic;

namespace Orbeden;

public abstract partial class Script
{
    //未能解析的引用路径表。加载场景时目标可能还没构造出来（或类型缺失），
    //此时字段值只能是 null；写回宿主时若直接取 null 的 Key 会把原路径刷成空，
    //所以把原路径留在这里，直到用户显式把该字段编辑为 null 才丢弃。
    private Dictionary<string, string>? unresolvedScalars;
    private Dictionary<string, List<string>>? unresolvedElements;

    //记录一个标量引用未能解析的稳定路径。
    internal void RememberUnresolvedReference(string fieldName, string path)
    {
        if (string.IsNullOrEmpty(path)) return;
        (unresolvedScalars ??= new Dictionary<string, string>(StringComparer.Ordinal))[fieldName] = path;
    }

    //读取标量引用的保留路径；已经解析或被显式清空时为空。
    internal string? GetUnresolvedReference(string fieldName)
    {
        return unresolvedScalars != null && unresolvedScalars.TryGetValue(fieldName, out string? path) ? path : null;
    }

    //记录集合中某个元素未能解析的稳定路径。
    internal void RememberUnresolvedElement(string fieldName, int index, string path)
    {
        if (index < 0) return;
        unresolvedElements ??= new Dictionary<string, List<string>>(StringComparer.Ordinal);
        if (!unresolvedElements.TryGetValue(fieldName, out List<string>? paths))
        {
            paths = [];
            unresolvedElements.Add(fieldName, paths);
        }
        while (paths.Count <= index) paths.Add(string.Empty);
        paths[index] = path;
    }

    //读取集合元素的保留路径；该下标没有保留值或已被显式清空时为空。
    internal string? GetUnresolvedElement(string fieldName, int index)
    {
        if (index < 0 || unresolvedElements == null
            || !unresolvedElements.TryGetValue(fieldName, out List<string>? paths)
            || index >= paths.Count) return null;
        string path = paths[index];
        return string.IsNullOrEmpty(path) ? null : path;
    }

    //丢弃一个字段的全部保留路径；字段被显式编辑后调用。
    internal void ClearUnresolvedReferences(string fieldName)
    {
        unresolvedScalars?.Remove(fieldName);
        unresolvedElements?.Remove(fieldName);
    }

    //程序集卸载或世界分离前清空路径表，避免静态引用滞留。
    internal void ClearAllUnresolvedReferences()
    {
        unresolvedScalars?.Clear();
        unresolvedElements?.Clear();
    }
}
