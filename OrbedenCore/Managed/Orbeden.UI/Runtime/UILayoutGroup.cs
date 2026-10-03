using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 排列子节点的容器基类。实现测量与排列两个接口，由布局重建按
/// 测宽 → 排 X/宽 → 测高 → 排 Y/高 的顺序驱动。
/// 排列结果写进子节点的解析缓存，不改动子节点的锚点配置。
/// </summary>
public abstract class UILayoutGroup : UIElement, IUILayoutMeasure, IUILayoutController
{
    private readonly List<UINode> children = [];
    private uint childrenStamp;

    /// <summary>创建布局组包装。</summary>
    protected UILayoutGroup(Ens ens) : base(ens)
    {
    }

    /// <summary>本组自己的解析矩形；由布局重建在排列前写入，局部空间。</summary>
    protected UIRect ArrangedRect { get; private set; }

    /// <summary>有效直接子节点：活动、有布局组件、且 ignoreLayout 为假。</summary>
    protected IReadOnlyList<UINode> ArrangedChildren
    {
        get
        {
            RefreshChildren();
            return children;
        }
    }

    /// <summary>测量期望宽度。</summary>
    public abstract float MeasureWidth();

    /// <summary>测量给定可用宽度下的期望高度。</summary>
    public abstract float MeasureHeight(float availableWidth);

    /// <summary>排列水平位置与宽度。</summary>
    public abstract void ArrangeHorizontal();

    /// <summary>排列垂直位置与高度。</summary>
    public abstract void ArrangeVertical();

    /// <summary>设置本次排列使用的容器矩形，由布局重建调用。</summary>
    internal void SetArrangedRect(UIRect rect)
    {
        ArrangedRect = rect;
    }

    /// <summary>请求下一次布局重建重新读取子节点列表。</summary>
    internal void InvalidateChildren()
    {
        childrenStamp = 0;
        UILayoutGroupCache.Invalidate();
    }

    //子节点列表按节点索引重建；重建期间不调用 GetComponents，也不分配临时数组。
    private void RefreshChildren()
    {
        uint stamp = UILayoutGroupCache.Stamp;
        if (childrenStamp == stamp && stamp != 0) return;
        childrenStamp = stamp;

        children.Clear();
        foreach (UINode child in UIWorldContext.GetChildNodes(this))
        {
            if (!child.IsRenderable || child.Layout == null) continue;
            if (child.Layout.GetIgnoreLayout()) continue;
            if (!IsNodeActive(child)) continue;
            children.Add(child);
        }
    }

    //子节点活动状态由所属 Ens 的层级活动决定。
    private static bool IsNodeActive(UINode node)
    {
        Ens owner = Ens.FromId(node.Ens);
        return owner.IsValid && owner.WorldActive;
    }

    /// <summary>按给定矩形与锚点落点写回子节点的解析结果；锚点配置本身不改动。</summary>
    protected void ApplyChildRect(UINode child, vector2 min, vector2 size, vector2 anchorPoint)
    {
        child.Layout?.SetResolvedRect(new UIRect(min, size), anchorPoint);
    }
}

/// <summary>
/// 布局组子节点缓存的版本号。任何组成员或层级变化都推进它，
/// 所有组的缓存因此一次性失效，不需要逐组通知。
/// </summary>
internal static class UILayoutGroupCache
{
    private static uint stamp;

    internal static uint Stamp
    {
        get
        {
            if (stamp == 0) ++stamp;
            return stamp;
        }
    }

    internal static void Invalidate()
    {
        ++stamp;
        if (stamp == 0) ++stamp;
    }
}
