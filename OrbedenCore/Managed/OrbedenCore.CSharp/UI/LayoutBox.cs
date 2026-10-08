using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>布局组的主轴方向。</summary>
public enum UILayoutOrientation : uint
{
    /// <summary>子节点从上向下排列。</summary>
    Vertical = 0,

    /// <summary>子节点从左向右排列。</summary>
    Horizontal = 1,
}

/// <summary>子节点在交叉轴上的对齐方式。</summary>
public enum UILayoutCrossAlignment : uint
{
    /// <summary>对齐交叉轴起点（水平排列时是下边）。</summary>
    Start = 0,

    /// <summary>对齐交叉轴中心。</summary>
    Center = 1,

    /// <summary>对齐交叉轴终点（水平排列时是上边）。</summary>
    End = 2,

    /// <summary>铺满整个交叉轴可用长度。</summary>
    Stretch = 3,
}

/// <summary>
/// 单方向顺序排列。主轴按子节点期望尺寸依次排开，不压缩也不按权重分配；
/// 交叉轴按 crossAlignment 对齐或铺满。
/// </summary>
public class LayoutBox : UILayoutGroup
{
    [SerializeField] private UILayoutOrientation orientation = UILayoutOrientation.Vertical;
    [SerializeField] private float spacing;
    [SerializeField] private float paddingLeft;
    [SerializeField] private float paddingRight;
    [SerializeField] private float paddingTop;
    [SerializeField] private float paddingBottom;
    [SerializeField] private UILayoutCrossAlignment crossAlignment = UILayoutCrossAlignment.Stretch;

    //排列阶段之间传递的子节点期望尺寸，避免同一帧重复测量。
    private readonly List<float> desiredWidths = [];
    private readonly List<float> desiredHeights = [];

    /// <summary>创建布局组包装。</summary>
    public LayoutBox(Ens ens) : base(ens)
    {
    }

    /// <summary>主轴方向。</summary>
    public UILayoutOrientation GetOrientation() => orientation;

    /// <summary>设置主轴方向。</summary>
    public void SetOrientation(UILayoutOrientation value)
    {
        if (orientation == value) return;
        orientation = value;
        MarkGroupDirty();
    }

    /// <summary>主轴间距，最小 0。</summary>
    public float GetSpacing() => spacing;

    /// <summary>设置主轴间距；负值与非有限值拒绝写入。</summary>
    public void SetSpacing(float value)
    {
        if (!IsLength(value) || spacing == value) return;
        spacing = value;
        MarkGroupDirty();
    }

    /// <summary>左边距。</summary>
    public float GetPaddingLeft() => paddingLeft;

    /// <summary>设置左边距；负值与非有限值拒绝写入。</summary>
    public void SetPaddingLeft(float value) => SetPadding(ref paddingLeft, value, nameof(paddingLeft));

    /// <summary>右边距。</summary>
    public float GetPaddingRight() => paddingRight;

    /// <summary>设置右边距；负值与非有限值拒绝写入。</summary>
    public void SetPaddingRight(float value) => SetPadding(ref paddingRight, value, nameof(paddingRight));

    /// <summary>上边距。</summary>
    public float GetPaddingTop() => paddingTop;

    /// <summary>设置上边距；负值与非有限值拒绝写入。</summary>
    public void SetPaddingTop(float value) => SetPadding(ref paddingTop, value, nameof(paddingTop));

    /// <summary>下边距。</summary>
    public float GetPaddingBottom() => paddingBottom;

    /// <summary>设置下边距；负值与非有限值拒绝写入。</summary>
    public void SetPaddingBottom(float value) => SetPadding(ref paddingBottom, value, nameof(paddingBottom));

    /// <summary>交叉轴对齐方式。</summary>
    public UILayoutCrossAlignment GetCrossAlignment() => crossAlignment;

    /// <summary>设置交叉轴对齐方式。</summary>
    public void SetCrossAlignment(UILayoutCrossAlignment value)
    {
        if (crossAlignment == value) return;
        crossAlignment = value;
        MarkGroupDirty();
    }

    /// <summary>主轴方向上的期望尺寸为各子期望尺寸之和加间距与边距；交叉轴取最大期望尺寸加边距。</summary>
    public override float MeasureWidth()
    {
        IReadOnlyList<UINode> children = ArrangedChildren;
        MeasureChildren(children, float.PositiveInfinity);

        bool horizontal = orientation == UILayoutOrientation.Horizontal;
        vector2 size = UILayoutMath.LayoutBoxSize(horizontal, Children(desiredWidths), [],
            spacing, paddingLeft, paddingRight, paddingTop, paddingBottom);
        return size.x;
    }

    /// <summary>给定可用宽度下的期望高度。交叉轴子节点的期望高度会用到这个宽度。</summary>
    public override float MeasureHeight(float availableWidth)
    {
        IReadOnlyList<UINode> children = ArrangedChildren;
        //单独调用时宽度可能还没量过，这里补齐，避免交叉轴取到空集合。
        if (desiredWidths.Count != children.Count) MeasureChildren(children, float.PositiveInfinity);

        float crossAvailable = orientation == UILayoutOrientation.Horizontal
            ? float.PositiveInfinity
            : MathF.Max(0.0f, availableWidth - (paddingLeft + paddingRight));
        MeasureHeights(children, crossAvailable);

        bool horizontal = orientation == UILayoutOrientation.Horizontal;
        vector2 size = UILayoutMath.LayoutBoxSize(horizontal, Children(desiredHeights), Children(desiredWidths),
            spacing, paddingLeft, paddingRight, paddingTop, paddingBottom);
        return size.y;
    }

    /// <summary>按主轴方向排列子节点的位置与长度；交叉轴只写对齐结果里与水平相关的一侧。</summary>
    public override void ArrangeHorizontal()
    {
        IReadOnlyList<UINode> children = ArrangedChildren;
        UIRect rect = ArrangedRect;

        if (orientation == UILayoutOrientation.Horizontal)
        {
            //主轴从左向右：主轴不压缩，子节点取自己的期望宽度。
            float cursor = rect.min.x + paddingLeft;
            for (int index = 0; index < children.Count; ++index)
            {
                float width = index < desiredWidths.Count ? desiredWidths[index] : 0.0f;
                ApplyChildHorizontal(children[index], cursor, width);
                cursor += width + spacing;
            }
            return;
        }

        //垂直组：水平方向是交叉轴。
        float available = MathF.Max(0.0f, rect.size.x - paddingLeft - paddingRight);
        for (int index = 0; index < children.Count; ++index)
        {
            float width = crossAlignment == UILayoutCrossAlignment.Stretch
                ? available
                : index < desiredWidths.Count ? desiredWidths[index] : 0.0f;
            float x = CrossStart(rect.min.x + paddingLeft, available, width);
            ApplyChildHorizontal(children[index], x, width);
        }
    }

    /// <summary>按主轴方向排列子节点的位置与长度；交叉轴只写对齐结果里与垂直相关的一侧。</summary>
    public override void ArrangeVertical()
    {
        IReadOnlyList<UINode> children = ArrangedChildren;
        UIRect rect = ArrangedRect;

        if (orientation == UILayoutOrientation.Vertical)
        {
            //主轴从上向下：第一个子节点贴容器上沿，之后的依次往下。
            float top = rect.min.y + rect.size.y - paddingTop;
            for (int index = 0; index < children.Count; ++index)
            {
                float height = index < desiredHeights.Count ? desiredHeights[index] : 0.0f;
                float min = top - height;
                ApplyChildVertical(children[index], min, height);
                top = min - spacing;
            }
            return;
        }

        //水平组：垂直方向是交叉轴。
        float available = MathF.Max(0.0f, rect.size.y - paddingTop - paddingBottom);
        float bottom = rect.min.y + paddingBottom;
        for (int index = 0; index < children.Count; ++index)
        {
            float height = crossAlignment == UILayoutCrossAlignment.Stretch
                ? available
                : index < desiredHeights.Count ? desiredHeights[index] : 0.0f;
            float y = CrossStart(bottom, available, height);
            ApplyChildVertical(children[index], y, height);
        }
    }

    //写回水平轴；垂直轴保留当前解析结果，由同一轮的后一个排列阶段覆盖。
    private void ApplyChildHorizontal(UINode child, float x, float width)
    {
        UIRect current = child.Layout?.GetResolvedRect() ?? new UIRect(new vector2(0.0f, 0.0f), new vector2(0.0f, 0.0f));
        ApplyChild(child, new vector2(x, current.min.y), new vector2(width, current.size.y));
    }

    //写回垂直轴；水平轴保留当前解析结果。
    private void ApplyChildVertical(UINode child, float y, float height)
    {
        UIRect current = child.Layout?.GetResolvedRect() ?? new UIRect(new vector2(0.0f, 0.0f), new vector2(0.0f, 0.0f));
        ApplyChild(child, new vector2(current.min.x, y), new vector2(current.size.x, height));
    }

    //把解析矩形与锚点落点写回子节点：Q = min + pivot * size，与本节点局部矩形互为逆运算。
    private void ApplyChild(UINode child, vector2 min, vector2 size)
    {
        if (child.Layout is not UILayout layout) return;
        vector2 pivot = layout.GetPivot();
        vector2 anchorPoint = new(min.x + pivot.x * size.x, min.y + pivot.y * size.y);
        layout.SetResolvedRect(new UIRect(min, size), anchorPoint);
    }

    //交叉轴起点交给共享公式；Stretch 与 Start 都贴起点。
    private float CrossStart(float start, float available, float size) =>
        UILayoutMath.CrossAxisStart(crossAlignment, start, available, size);

    //测量全部子节点的期望宽度。
    private void MeasureChildren(IReadOnlyList<UINode> children, float availableWidth)
    {
        desiredWidths.Clear();
        for (int index = 0; index < children.Count; ++index)
            desiredWidths.Add(DesiredWidth(children[index], availableWidth));
    }

    //测量全部子节点的期望高度。
    private void MeasureHeights(IReadOnlyList<UINode> children, float availableWidth)
    {
        desiredHeights.Clear();
        for (int index = 0; index < children.Count; ++index)
            desiredHeights.Add(DesiredHeight(children[index], availableWidth));
    }

    //子节点期望宽度：开了 fitWidth 用测量值，否则退回 sizeDelta 的非负部分。
    internal static float DesiredWidth(UINode node, float availableWidth)
    {
        UILayout? layout = node.Layout;
        if (layout == null) return 0.0f;
        float delta = MathF.Max(0.0f, layout.GetSizeDelta().x);
        if (!layout.GetFitWidth()) return delta;
        IUILayoutMeasure? source = node.GetMeasureSource();
        if (source == null) return delta;
        float measured = source.MeasureWidth();
        return float.IsFinite(measured) && measured >= 0.0f ? measured : delta;
    }

    //子节点期望高度：开了 fitHeight 用给定宽度下的测量值，否则退回 sizeDelta 的非负部分。
    internal static float DesiredHeight(UINode node, float availableWidth)
    {
        UILayout? layout = node.Layout;
        if (layout == null) return 0.0f;
        float delta = MathF.Max(0.0f, layout.GetSizeDelta().y);
        if (!layout.GetFitHeight()) return delta;
        IUILayoutMeasure? source = node.GetMeasureSource();
        if (source == null) return delta;
        float measured = source.MeasureHeight(availableWidth);
        if (!float.IsFinite(measured) || measured < 0.0f) return delta;
        return measured;
    }

    //把列表交给共享公式；ReadOnlySpan 不能直接从 List 取，这里复制成数组以便复用同一段实现。
    private static float[] Children(List<float> values) => values.ToArray();

    private void SetPadding(ref float target, float value, string name)
    {
        if (!IsLength(value) || target == value) return;
        target = value;
        MarkGroupDirty();
    }

    private static bool IsLength(float value) => float.IsFinite(value) && value >= 0.0f;

    private void MarkGroupDirty()
    {
        InvalidateChildren();
        UIWorldContext.MarkLayoutDirty(GetLayout());
    }
}
