using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 图形重建。几何与材质分成两个脏集合：几何重建会重跑 PopulateMesh，
/// 材质变化只把状态并进已有片段。布局尺寸变化会在这里补进几何脏集合。
/// </summary>
internal static class UIGraphicRegistry
{
    /// <summary>执行一次图形重建；同一元素不会被重建两次。</summary>
    internal static void Rebuild(HashSet<UIElement> geometryDirty)
    {
        if (geometryDirty.Count == 0) return;

        foreach (UIElement element in geometryDirty)
        {
            if (element is UIVisual visual) visual.Rebuild();
        }
        geometryDirty.Clear();
    }

    /// <summary>把一棵子树上尺寸发生变化的图形补进几何脏集合。</summary>
    internal static void MarkResizedGeometry(UINode node, HashSet<UIElement> geometryDirty)
    {
        if (node.Layout != null && node.Layout.ConsumeSizeChanged() && node.Visual is UIVisual visual)
            geometryDirty.Add(visual);

        foreach (UINode child in node.Children) MarkResizedGeometry(child, geometryDirty);
    }

    /// <summary>把一棵子树上外部依赖失效的图形补进几何脏集合；例如字形页被回收。</summary>
    internal static void MarkInvalidatedGeometry(UINode node, HashSet<UIElement> geometryDirty)
    {
        if (node.Visual is UIVisual visual && visual.IsGeometryInvalidated()) geometryDirty.Add(visual);

        foreach (UINode child in node.Children) MarkInvalidatedGeometry(child, geometryDirty);
    }
}
