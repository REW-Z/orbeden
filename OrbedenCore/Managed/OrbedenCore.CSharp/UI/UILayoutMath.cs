using System;

namespace Orbeden;

/// <summary>
/// 布局的纯计算部分。与组件分离是为了让公式可以脱离原生宿主单独验证：
/// 组件负责校验与标脏，这里只做数值换算，不读写任何世界状态。
/// </summary>
public static class UILayoutMath
{
    /// <summary>
    /// 按锚点、轴心、偏移与尺寸增量解析一个矩形。父矩形以父级局部空间给出，
    /// 返回的矩形在自身局部空间（左下角为 -pivot * size），anchorPoint 是设计公式里的 Q。
    /// </summary>
    public static UIRect Resolve(
        vector2 anchorMin, vector2 anchorMax, vector2 pivot, vector2 offset, vector2 sizeDelta,
        vector2 parentMin, vector2 parentSize, out vector2 anchorPoint)
    {
        anchorPoint = new vector2(0.0f, 0.0f);
        if (!IsFinite(parentMin) || !IsFinite(parentSize))
            return new UIRect(new vector2(0.0f, 0.0f), new vector2(0.0f, 0.0f));

        vector2 amin = new(parentMin.x + parentSize.x * anchorMin.x, parentMin.y + parentSize.y * anchorMin.y);
        vector2 amax = new(parentMin.x + parentSize.x * anchorMax.x, parentMin.y + parentSize.y * anchorMax.y);
        vector2 span = new(amax.x - amin.x, amax.y - amin.y);
        vector2 size = new(MathF.Max(0.0f, span.x + sizeDelta.x), MathF.Max(0.0f, span.y + sizeDelta.y));
        anchorPoint = new vector2(amin.x + span.x * pivot.x + offset.x, amin.y + span.y * pivot.y + offset.y);
        return new UIRect(new vector2(-pivot.x * size.x, -pivot.y * size.y), size);
    }

    /// <summary>
    /// 按显示区域的像素尺寸计算画布缩放。
    /// 常量像素模式直接取 scaleFactor；参考分辨率模式取宽高两个对数缩放的加权插值。
    /// </summary>
    public static float ComputeScale(
        CanvasScaleMode scaleMode, float scaleFactor, vector2 referenceResolution,
        float matchWidthOrHeight, vector2 targetPixelSize)
    {
        if (scaleMode == CanvasScaleMode.ConstantPixel) return scaleFactor;
        if (targetPixelSize.x <= 0.0f || targetPixelSize.y <= 0.0f) return scaleFactor;
        if (referenceResolution.x <= 0.0f || referenceResolution.y <= 0.0f) return scaleFactor;

        float widthRatio = targetPixelSize.x / referenceResolution.x;
        float heightRatio = targetPixelSize.y / referenceResolution.y;
        if (widthRatio <= 0.0f || heightRatio <= 0.0f) return scaleFactor;

        float blended = MathF.Pow(2.0f,
            (1.0f - matchWidthOrHeight) * MathF.Log2(widthRatio)
            + matchWidthOrHeight * MathF.Log2(heightRatio));
        return scaleFactor * blended;
    }

    /// <summary>按显示区域的像素尺寸计算逻辑尺寸；缩放无效时夹紧到下限 0.01。</summary>
    public static vector2 ComputeLogicalSize(
        CanvasScaleMode scaleMode, float scaleFactor, vector2 referenceResolution,
        float matchWidthOrHeight, vector2 targetPixelSize)
    {
        float scale = ComputeScale(scaleMode, scaleFactor, referenceResolution, matchWidthOrHeight, targetPixelSize);
        if (!float.IsFinite(scale) || scale < 0.01f) scale = 0.01f;
        return new vector2(targetPixelSize.x / scale, targetPixelSize.y / scale);
    }

    /// <summary>
    /// 顺序排列容器的期望尺寸。主轴为各子期望尺寸之和加间距与边距，交叉轴取最大期望尺寸加边距；
    /// 没有子节点时两个轴都只剩边距。
    /// </summary>
    public static vector2 LayoutBoxSize(bool horizontal, ReadOnlySpan<float> mainSizes, ReadOnlySpan<float> crossSizes,
        float spacing, float padLeft, float padRight, float padTop, float padBottom)
    {
        float padX = padLeft + padRight;
        float padY = padTop + padBottom;
        if (mainSizes.Length == 0) return new vector2(padX, padY);

        float gap = spacing * (mainSizes.Length - 1);
        float main = Sum(mainSizes) + gap;
        float cross = Maximum(crossSizes);
        return horizontal
            ? new vector2(main + padX, cross + padY)
            : new vector2(cross + padX, main + padY);
    }

    /// <summary>网格容器的期望尺寸：行列数乘单元格尺寸加间距与边距；没有子节点时只剩边距。</summary>
    public static vector2 GridBoxSize(int columns, int rows, int count, vector2 cellSize, vector2 spacing,
        float padLeft, float padRight, float padTop, float padBottom)
    {
        float padX = padLeft + padRight;
        float padY = padTop + padBottom;
        if (count <= 0) return new vector2(padX, padY);
        float width = columns * cellSize.x + spacing.x * MathF.Max(columns - 1, 0) + padX;
        float height = rows * cellSize.y + spacing.y * MathF.Max(rows - 1, 0) + padY;
        return new vector2(width, height);
    }

    /// <summary>交叉轴上的起点：Start/Stretch 贴起点，Center 居中，End 贴终点。</summary>
    public static float CrossAxisStart(UILayoutCrossAlignment alignment, float start, float available, float size)
    {
        return alignment switch
        {
            UILayoutCrossAlignment.Center => start + (available - size) * 0.5f,
            UILayoutCrossAlignment.End => start + available - size,
            _ => start,
        };
    }

    /// <summary>按行优先顺序把子节点下标换算成行列。</summary>
    public static void GridIndices(int index, int columns, out int column, out int row)
    {
        if (columns <= 1)
        {
            column = 0;
            row = index;
            return;
        }
        column = index % columns;
        row = index / columns;
    }

    /// <summary>自动列数：按可用宽度能放下几列，至少一列；步长为负时退回一列。</summary>
    public static int AutoColumnCount(float availableWidth, float cellWidth, float spacing)
    {
        float step = cellWidth + spacing;
        if (step <= 0.0f) return 1;
        return Math.Max(1, (int)MathF.Floor((availableWidth + spacing) / step));
    }

    /// <summary>按约束解出行列数；AutoColumns 用可用宽度推导列数，随后沿用固定列的索引方式。</summary>
    public static void ResolveGrid(UIGridConstraint constraint, int constraintCount, int count,
        float cellWidth, float spacing, float availableWidth, out int columns, out int rows)
    {
        int fixedCount = Math.Max(1, constraintCount);
        if (count <= 0)
        {
            columns = constraint == UIGridConstraint.FixedRows ? 1 : fixedCount;
            rows = constraint == UIGridConstraint.FixedRows ? fixedCount : 1;
            return;
        }

        switch (constraint)
        {
        case UIGridConstraint.FixedRows:
            rows = fixedCount;
            columns = (count + rows - 1) / rows;
            break;
        case UIGridConstraint.AutoColumns:
            columns = AutoColumnCount(availableWidth, cellWidth, spacing);
            rows = (count + columns - 1) / columns;
            break;
        default:
            columns = fixedCount;
            rows = (count + columns - 1) / columns;
            break;
        }
        columns = Math.Max(1, columns);
        rows = Math.Max(1, rows);
    }

    /// <summary>两个分量都有限时返回真。</summary>
    public static bool IsFinite(vector2 value) => float.IsFinite(value.x) && float.IsFinite(value.y);

    private static float Sum(ReadOnlySpan<float> values)
    {
        float total = 0.0f;
        foreach (float value in values) total += value;
        return total;
    }

    private static float Maximum(ReadOnlySpan<float> values)
    {
        float result = 0.0f;
        foreach (float value in values) result = MathF.Max(result, value);
        return result;
    }
}
