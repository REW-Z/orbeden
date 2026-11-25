using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 布局重建。脏请求提升到受影响的测量容器并按子树去重，再按
/// 自底向上测宽 → 自顶向下排 X/宽 → 自底向上测高 → 自顶向下排 Y/高 遍历，
/// 最后把全部派生位置合成一次批量提交。重建期间产生的新请求留到下一次刷新。
/// </summary>
internal static class UILayoutRegistry
{
    //本次重建中每个节点量出的期望尺寸；测量只依赖已确定的上阶段数据，不做迭代求不动点。
    private static readonly Dictionary<UINode, float> measuredWidths = [];
    private static readonly Dictionary<UINode, float> measuredHeights = [];

    /// <summary>重建受影响的布局子树，并批量提交派生位置。</summary>
    internal static void Rebuild(UIWorldContext context, HashSet<UILayout> dirty, HashSet<UILayout> positionDirty)
    {
        //区分锚点平移与完整布局
        List<UINode> positionNodes = [];
        foreach (UILayout layout in positionDirty)
        {
            UINode? node = context.FindNode(layout.EnsId);
            if (node == null || !ReferenceEquals(node.Layout, layout) || dirty.Contains(layout))
            {
                continue;
            }

            if (CanResolvePosition(context, node, layout))
            {
                positionNodes.Add(node);
            }
            else
            {
                dirty.Add(layout);
            }
        }
        positionDirty.Clear();

        List<UINode> roots = Promote(context, dirty);
        dirty.Clear();

        measuredWidths.Clear();
        measuredHeights.Clear();

        foreach (UINode root in roots)
        {
            if (root.Layout == null || root.ConfigurationError.Length != 0)
            {
                continue;
            }

            //按画布目标或已解析的父矩形初始化子树
            if (root.Canvas != null)
            {
                vector2 size = context.ResolveCanvasLogicalSize(root.Canvas);
                vector2 pivot = root.Layout.GetPivot();
                vector2 min = root.Canvas.GetRenderMode() == CanvasRenderMode.WorldSpace
                    ? new vector2(-pivot.x * size.x, -pivot.y * size.y) : default;
                root.Layout.SetResolvedRect(new UIRect(min, size), default);
            }
            MeasureWidths(root);
            UIRect parentRect = root.Parent?.Layout?.GetResolvedRect() ?? default;
            ArrangeWidths(root, root.Canvas != null, parentRect.min, parentRect.size);
            float availableWidth = RootAvailableWidth(context, root);
            MeasureHeights(root, availableWidth);
            ArrangeHeights(root, true, default, default);
        }

        List<UIDerivedPosition> positions = [];
        foreach (UINode root in roots)
        {
            CollectPositions(root, positions);
        }

        //读取当前父矩形并更新移动节点的落点
        foreach (UINode node in positionNodes)
        {
            UINode? ancestor = node;
            while (ancestor != null && !roots.Contains(ancestor))
            {
                ancestor = ancestor.Parent;
            }
            if (ancestor != null)
            {
                continue;
            }

            UILayout layout = node.Layout!;
            UIRect parentRect = node.Parent!.Layout!.GetResolvedRect();
            layout.Resolve(parentRect.min, parentRect.size, out vector2 anchor);
            layout.SetResolvedRect(layout.GetResolvedRect(), anchor);
            CollectPosition(node, positions);
        }
        context.CommitDerivedPositions(positions);

        measuredWidths.Clear();
        measuredHeights.Clear();
    }

    /// <summary>清空解析缓存；世界分离时调用。</summary>
    internal static void Clear(IEnumerable<UINode> nodes)
    {
        foreach (UINode node in nodes) node.Layout?.ClearResolvedRect();
    }

    //判断节点是否能单独更新锚点落点
    private static bool CanResolvePosition(UIWorldContext context, UINode node, UILayout layout)
    {
        if (node.Canvas != null || !layout.HasResolvedRect || layout.HasDrivenRect || layout.HasDrivenOffset
            || node.Parent?.Layout is not { HasResolvedRect: true })
        {
            return false;
        }

        for (UINode? current = node; current != null; current = current.Parent)
        {
            if (current.ConfigurationError.Length != 0 || current.GetElement<UILayoutGroup>() != null
                || current.Layout is { } currentLayout && (currentLayout.GetFitWidth() || currentLayout.GetFitHeight()))
            {
                return false;
            }
        }
        return CanReuseChildLayout(context, node);
    }

    //检查后代是否需要重新排列或重新提交派生位置
    private static bool CanReuseChildLayout(UIWorldContext context, UINode node)
    {
        foreach (UINode child in node.Children)
        {
            if (child.Layout is not UILayout childLayout || child.ConfigurationError.Length != 0)
            {
                continue;
            }
            if (!childLayout.HasResolvedRect || child.GetElement<UILayoutGroup>() != null
                || context.HasClearedDerivedPosition(child.Ens) || !CanReuseChildLayout(context, child))
            {
                return false;
            }
        }
        return true;
    }

    //把脏节点提升到依赖其测量的容器，并去掉已覆盖的后代
    private static List<UINode> Promote(UIWorldContext context, HashSet<UILayout> dirty)
    {
        List<UINode> roots = [];
        foreach (UILayout layout in dirty)
        {
            UINode? node = context.FindNode(layout.EnsId);
            if (node == null) continue;

            UINode root = node;
            for (UINode? ancestor = node.Parent; ancestor != null; ancestor = ancestor.Parent)
            {
                if (ancestor.GetElement<UILayoutGroup>() != null
                    || ancestor.Layout is { } ancestorLayout && (ancestorLayout.GetFitWidth() || ancestorLayout.GetFitHeight()))
                {
                    root = ancestor;
                }
            }
            if (!roots.Contains(root))
            {
                roots.Add(root);
            }
        }

        //祖先已经在队列里时，后代的重建由祖先的遍历覆盖。
        for (int index = roots.Count - 1; index >= 0; --index)
        {
            for (UINode? current = roots[index].Parent; current != null; current = current.Parent)
            {
                if (!roots.Contains(current)) continue;
                roots.RemoveAt(index);
                break;
            }
        }
        return roots;
    }

    //第一阶段：自底向上测宽。组的测量会递归到子组，因此从根调用一次即可覆盖整棵子树。
    private static void MeasureWidths(UINode node)
    {
        UILayout? layout = node.Layout;
        if (layout == null || node.ConfigurationError.Length != 0) return;

        foreach (UINode child in node.Children) MeasureWidths(child);

        if (node.GetElement<UILayoutGroup>() is not UILayoutGroup group) return;
        float measured = group.MeasureWidth();
        if (float.IsFinite(measured)) measuredWidths[node] = measured;
    }

    //第三阶段：自底向上测高。可用宽度取已经排好的宽度。
    private static void MeasureHeights(UINode node, float availableWidth)
    {
        UILayout? layout = node.Layout;
        if (layout == null || node.ConfigurationError.Length != 0) return;

        if (node.GetElement<UILayoutGroup>() is UILayoutGroup group)
        {
            float measured = group.MeasureHeight(availableWidth);
            if (float.IsFinite(measured)) measuredHeights[node] = measured;
        }

        //尚未排过高的子节点先按当前解析宽度继续往下传。
        float childAvailable = layout.HasResolvedRect ? layout.GetResolvedRect().size.x : availableWidth;
        foreach (UINode child in node.Children) MeasureHeights(child, childAvailable);
    }

    //第二阶段：自顶向下排 X 与宽。父组已经把子节点排好时只负责继续往下递归。
    private static void ArrangeWidths(UINode node, bool rectAlreadySet, vector2 parentMin, vector2 parentSize)
    {
        UILayout? layout = node.Layout;
        if (layout == null || node.ConfigurationError.Length != 0) return;

        if (!rectAlreadySet)
        {
            //被控件驱动的矩形直接采用覆盖值：子布局的锚点偏移不参与。
            UIRect rect = layout.HasDrivenRect
                ? layout.ResolveDriven(out vector2 drivenAnchor)
                : layout.Resolve(parentMin, parentSize, out drivenAnchor);
            //平移覆盖叠加在解析结果之上：滚动容器的内容靠它移动，锚点落点也跟着走。
            if (layout.HasDrivenOffset)
            {
                vector2 offset = layout.GetDrivenOffset();
                rect = new UIRect(new vector2(rect.min.x + offset.x, rect.min.y + offset.y), rect.size);
                drivenAnchor = new vector2(drivenAnchor.x + offset.x, drivenAnchor.y + offset.y);
            }
            layout.SetResolvedRect(FitRect(node, rect), drivenAnchor);
        }

        if (node.GetElement<UILayoutGroup>() is UILayoutGroup group)
        {
            //组自己排列直接子节点，递归只负责继续处理更下一层。
            group.SetArrangedRect(layout.GetResolvedRect());
            group.ArrangeHorizontal();
            foreach (UINode child in node.Children) ArrangeWidths(child, true, default, default);
            return;
        }

        UIRect resolved = layout.GetResolvedRect();
        foreach (UINode child in node.Children) ArrangeWidths(child, false, resolved.min, resolved.size);
    }

    //第四阶段：自顶向下排 Y 与高。
    private static void ArrangeHeights(UINode node, bool rectAlreadySet, vector2 parentMin, vector2 parentSize)
    {
        UILayout? layout = node.Layout;
        if (layout == null || node.ConfigurationError.Length != 0) return;

        if (node.GetElement<UILayoutGroup>() is UILayoutGroup group)
        {
            group.SetArrangedRect(layout.GetResolvedRect());
            group.ArrangeVertical();
            foreach (UINode child in node.Children) ArrangeHeights(child, true, default, default);
            return;
        }

        UIRect resolved = layout.GetResolvedRect();
        foreach (UINode child in node.Children) ArrangeHeights(child, false, resolved.min, resolved.size);
    }

    //fit 生效时用测量尺寸替换解析尺寸；位置仍由锚点决定，局部 min 随尺寸重算。
    private static UIRect FitRect(UINode node, UIRect rect)
    {
        UILayout? layout = node.Layout;
        if (layout == null) return rect;

        float width = rect.size.x;
        float height = rect.size.y;
        if (layout.GetFitWidth() && measuredWidths.TryGetValue(node, out float measuredWidth)) width = measuredWidth;
        if (layout.GetFitHeight() && measuredHeights.TryGetValue(node, out float measuredHeight)) height = measuredHeight;
        if (width == rect.size.x && height == rect.size.y) return rect;

        vector2 pivot = layout.GetPivot();
        return new UIRect(new vector2(-pivot.x * width, -pivot.y * height), new vector2(width, height));
    }

    //画布根的可用宽度：屏幕画布用量出的逻辑尺寸，世界空间用根自身的尺寸增量。
    private static float RootAvailableWidth(UIWorldContext context, UINode root)
    {
        if (root.Layout == null)
        {
            return 0.0f;
        }
        if (root.Canvas == null)
        {
            return root.Layout.GetResolvedRect().Width;
        }
        if (root.Canvas.GetRenderMode() == CanvasRenderMode.WorldSpace)
        {
            return root.Layout.GetSizeDelta().x;
        }
        return context.ResolveCanvasLogicalSize(root.Canvas).x;
    }

    //遍历整棵子树，按解析出的锚点落点合成派生位置；画布根不参与投影，因此不写位置。
    private static void CollectPositions(UINode node, List<UIDerivedPosition> positions)
    {
        if (node.Layout == null || node.ConfigurationError.Length != 0) return;

        if (node.Canvas == null)
        {
            CollectPosition(node, positions);
        }

        foreach (UINode child in node.Children) CollectPositions(child, positions);
    }

    //收集节点落点并叠加屏幕画布的轴心偏移
    private static void CollectPosition(UINode node, List<UIDerivedPosition> positions)
    {
        vector3 position = node.Layout!.GetResolvedLocalPosition();
        vector2 canvasOffset = node.Parent is UINode parent ? CanvasPivotOffset(parent) : default;
        positions.Add(new UIDerivedPosition
        {
            ens = node.Ens,
            position = new vector3(position.x + canvasOffset.x, position.y + canvasOffset.y, position.z),
            clear = 0,
        });
    }

    //画布把根逻辑矩形摆到枢轴上的居中偏移；不是画布节点的父级返回零。
    private static vector2 CanvasPivotOffset(UINode canvasNode)
    {
        if (canvasNode.Canvas == null || canvasNode.Layout == null) return default;
        if (canvasNode.Canvas.GetRenderMode() == CanvasRenderMode.WorldSpace) return default;

        UIRect rect = canvasNode.Layout.GetResolvedRect();
        vector2 pivot = canvasNode.Layout.GetPivot();
        return new vector2(-pivot.x * rect.size.x, -pivot.y * rect.size.y);
    }
}
