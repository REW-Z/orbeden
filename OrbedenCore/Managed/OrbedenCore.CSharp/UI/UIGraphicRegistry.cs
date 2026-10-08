using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 图形重建。几何与材质分成两个脏集合：几何重建会重跑 PopulateMesh，
/// 材质变化只把状态并进已有片段。布局尺寸变化会在这里补进几何脏集合。
/// </summary>
internal static class UIGraphicRegistry
{
    /// <summary>执行一次图形重建；同一元素不会被重建两次。</summary>
    internal static void Rebuild(HashSet<UIElement> geometryDirty, List<UIElement> rebuilding)
    {
        if (geometryDirty.Count == 0) return;

        rebuilding.Clear();
        rebuilding.AddRange(geometryDirty);
        geometryDirty.Clear();
        foreach (UIElement element in rebuilding)
        {
            if (element is UIVisual visual) visual.Rebuild();
        }
        rebuilding.Clear();
    }

    /// <summary>检查单个节点的尺寸变化，加入几何脏集合。</summary>
    internal static void MarkResizedGeometry(UINode node, HashSet<UIElement> geometryDirty)
    {
        if (node.Layout != null && node.Layout.ConsumeSizeChanged() && node.Visual is UIVisual visual)
            geometryDirty.Add(visual);

    }

    /// <summary>检查单个节点的外部依赖，例如字形页回收。</summary>
    internal static void MarkInvalidatedGeometry(UINode node, HashSet<UIElement> geometryDirty)
    {
        if (node.Visual is UIVisual visual && visual.IsGeometryInvalidated()) geometryDirty.Add(visual);

    }
}
