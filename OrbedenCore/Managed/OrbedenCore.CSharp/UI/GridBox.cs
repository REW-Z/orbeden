using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>网格的行列约束方式。</summary>
public enum UIGridConstraint : uint
{
    /// <summary>固定列数，行数按子节点数量推导。</summary>
    FixedColumns = 0,

    /// <summary>固定行数，列数按子节点数量推导。</summary>
    FixedRows = 1,

    /// <summary>列数按容器可用宽度自动推导。</summary>
    AutoColumns = 2,
}

/// <summary>
/// 网格排列。单元格尺寸固定，从左上角开始按行列填充；尺寸只由约束与单元格尺寸决定，
/// 与子节点的期望尺寸无关。AutoColumns 需要容器宽度，因此与 fitWidth 互斥。
/// </summary>
public class GridBox : UILayoutGroup
{
    [SerializeField] private UIGridConstraint constraint = UIGridConstraint.FixedColumns;
    [SerializeField] private int constraintCount = 1;
    [SerializeField] private vector2 cellSize = new(100.0f, 100.0f);
    [SerializeField] private vector2 spacing = new(0.0f, 0.0f);
    [SerializeField] private float paddingLeft;
    [SerializeField] private float paddingRight;
    [SerializeField] private float paddingTop;
    [SerializeField] private float paddingBottom;

    //本次重建解出的行列数；AutoColumns 只有在排列阶段拿到容器宽度后才确定。
    private int resolvedColumns = 1;
    private int resolvedRows = 1;
    private bool configurationValid = true;

    /// <summary>创建网格组包装。</summary>
    public GridBox(Ens ens) : base(ens)
    {
    }

    /// <summary>行列约束方式。</summary>
    public UIGridConstraint GetConstraint() => constraint;

    /// <summary>设置行列约束方式。</summary>
    public void SetConstraint(UIGridConstraint value)
    {
        if (constraint == value) return;
        constraint = value;
        MarkGroupDirty();
    }

    /// <summary>约束数量，至少 1。</summary>
    public int GetConstraintCount() => constraintCount;

    /// <summary>设置约束数量；小于 1 拒绝写入。</summary>
    public void SetConstraintCount(int value)
    {
        if (value < 1 || constraintCount == value) return;
        constraintCount = value;
        MarkGroupDirty();
    }

    /// <summary>单元格尺寸，各轴至少 1。</summary>
    public vector2 GetCellSize() => cellSize;

    /// <summary>设置单元格尺寸；任一轴小于 1 或非有限时拒绝写入。</summary>
    public void SetCellSize(vector2 value)
    {
        if (!float.IsFinite(value.x) || !float.IsFinite(value.y) || value.x < 1.0f || value.y < 1.0f) return;
        if (cellSize.x == value.x && cellSize.y == value.y) return;
        cellSize = value;
        MarkGroupDirty();
    }

    /// <summary>行列间距，各轴不小于 0。</summary>
    public vector2 GetSpacing() => spacing;

    /// <summary>设置行列间距；任一轴为负或非有限时拒绝写入。</summary>
    public void SetSpacing(vector2 value)
    {
        if (!UILayoutMath.IsFinite(value) || value.x < 0.0f || value.y < 0.0f) return;
        if (spacing.x == value.x && spacing.y == value.y) return;
        spacing = value;
        MarkGroupDirty();
    }

    /// <summary>左边距。</summary>
    public float GetPaddingLeft() => paddingLeft;

    /// <summary>设置左边距；负值与非有限值拒绝写入。</summary>
    public void SetPaddingLeft(float value) => SetPadding(ref paddingLeft, value);

    /// <summary>右边距。</summary>
    public float GetPaddingRight() => paddingRight;

    /// <summary>设置右边距；负值与非有限值拒绝写入。</summary>
    public void SetPaddingRight(float value) => SetPadding(ref paddingRight, value);

    /// <summary>上边距。</summary>
    public float GetPaddingTop() => paddingTop;

    /// <summary>设置上边距；负值与非有限值拒绝写入。</summary>
    public void SetPaddingTop(float value) => SetPadding(ref paddingTop, value);

    /// <summary>下边距。</summary>
    public float GetPaddingBottom() => paddingBottom;

    /// <summary>设置下边距；负值与非有限值拒绝写入。</summary>
    public void SetPaddingBottom(float value) => SetPadding(ref paddingBottom, value);

    /// <summary>期望宽度：列数乘单元格宽加列间距与左右边距；没有子节点时只剩边距。</summary>
    public override float MeasureWidth()
    {
        configurationValid = ValidateConfiguration();
        int count = ArrangedChildren.Count;
        ResolveGrid(count, float.PositiveInfinity, out resolvedColumns, out resolvedRows);
        if (!configurationValid) return paddingLeft + paddingRight;
        return UILayoutMath.GridBoxSize(resolvedColumns, resolvedRows, count, cellSize, spacing,
            paddingLeft, paddingRight, paddingTop, paddingBottom).x;
    }

    /// <summary>期望高度：行数乘单元格高加行间距与上下边距；没有子节点时只剩边距。</summary>
    public override float MeasureHeight(float availableWidth)
    {
        configurationValid = ValidateConfiguration();
        int count = ArrangedChildren.Count;
        ResolveGrid(count, availableWidth, out resolvedColumns, out resolvedRows);
        if (!configurationValid) return paddingTop + paddingBottom;
        return UILayoutMath.GridBoxSize(resolvedColumns, resolvedRows, count, cellSize, spacing,
            paddingLeft, paddingRight, paddingTop, paddingBottom).y;
    }

    /// <summary>按列索引排列水平位置与单元格宽度，从左上角的列起点开始。</summary>
    public override void ArrangeHorizontal()
    {
        if (!configurationValid) return;
        IReadOnlyList<UINode> children = ArrangedChildren;
        ResolveGrid(children.Count, MathF.Max(0.0f, ArrangedRect.size.x - paddingLeft - paddingRight),
            out resolvedColumns, out resolvedRows);

        for (int index = 0; index < children.Count; ++index)
        {
            UILayoutMath.GridIndices(index, resolvedColumns, out int column, out _);
            float x = ArrangedRect.min.x + paddingLeft + column * (cellSize.x + spacing.x);
            ApplyAxis(children[index], x, cellSize.x, horizontal: true);
        }
    }

    /// <summary>按行索引排列垂直位置与单元格高度，首行贴容器上沿。</summary>
    public override void ArrangeVertical()
    {
        if (!configurationValid) return;
        IReadOnlyList<UINode> children = ArrangedChildren;
        ResolveGrid(children.Count, MathF.Max(0.0f, ArrangedRect.size.x - paddingLeft - paddingRight),
            out resolvedColumns, out resolvedRows);

        float top = ArrangedRect.min.y + ArrangedRect.size.y - paddingTop;
        for (int index = 0; index < children.Count; ++index)
        {
            UILayoutMath.GridIndices(index, resolvedColumns, out _, out int row);
            float y = top - row * (cellSize.y + spacing.y) - cellSize.y;
            ApplyAxis(children[index], y, cellSize.y, horizontal: false);
        }
    }

    //写回一个轴；另一个轴保留当前解析结果，由同一轮的后一个阶段覆盖。
    private void ApplyAxis(UINode child, float position, float length, bool horizontal)
    {
        if (child.Layout is not UILayout layout) return;
        UIRect current = layout.GetResolvedRect();
        vector2 min = horizontal ? new vector2(position, current.min.y) : new vector2(current.min.x, position);
        vector2 size = horizontal ? new vector2(length, current.size.y) : new vector2(current.size.x, length);
        vector2 pivot = layout.GetPivot();
        layout.SetResolvedRect(new UIRect(min, size),
            new vector2(min.x + pivot.x * size.x, min.y + pivot.y * size.y));
    }

    //按约束解出行列数；公式与测试共用同一份实现。
    private void ResolveGrid(int count, float availableWidth, out int columns, out int rows) =>
        UILayoutMath.ResolveGrid(constraint, constraintCount, count, cellSize.x, spacing.x, availableWidth, out columns, out rows);

    //AutoColumns 依赖容器宽度，与 fitWidth 互相依赖，属于禁止的配置组合。
    private bool ValidateConfiguration()
    {
        if (constraint != UIGridConstraint.AutoColumns) return true;
        return GetLayout() is not UILayout layout || !layout.GetFitWidth();
    }

    private void SetPadding(ref float target, float value)
    {
        if (!float.IsFinite(value) || value < 0.0f || target == value) return;
        target = value;
        MarkGroupDirty();
    }

    private void MarkGroupDirty()
    {
        InvalidateChildren();
        UIWorldContext.MarkLayoutDirty(GetLayout());
    }
}
