namespace Orbeden;

/// <summary>UI 解析矩形：左下角与尺寸，坐标系为右手系、X 向右、Y 向上。</summary>
public readonly struct UIRect
{
    /// <summary>矩形左下角。</summary>
    public readonly vector2 min;

    /// <summary>矩形尺寸，允许为 0。</summary>
    public readonly vector2 size;

    /// <summary>创建解析矩形。</summary>
    public UIRect(vector2 min, vector2 size)
    {
        this.min = min;
        this.size = size;
    }

    /// <summary>矩形宽度。</summary>
    public float Width => size.x;

    /// <summary>矩形高度。</summary>
    public float Height => size.y;

    /// <summary>矩形右上角。</summary>
    public vector2 Max => new(min.x + size.x, min.y + size.y);

    /// <summary>矩形中心。</summary>
    public vector2 Center => new(min.x + size.x * 0.5f, min.y + size.y * 0.5f);

    /// <summary>判断点是否落在矩形内。左/下边界包含，右/上边界不包含；零尺寸不命中。</summary>
    public bool Contains(vector2 point)
    {
        if (size.x <= 0.0f || size.y <= 0.0f) return false;
        return point.x >= min.x && point.x < min.x + size.x
            && point.y >= min.y && point.y < min.y + size.y;
    }

    /// <summary>判断点是否落在矩形内并包含右/上边界；用于覆盖统计等不关心邻接排重的场合。</summary>
    public bool ContainsInclusive(vector2 point)
    {
        if (size.x < 0.0f || size.y < 0.0f) return false;
        return point.x >= min.x && point.x <= min.x + size.x
            && point.y >= min.y && point.y <= min.y + size.y;
    }

    /// <summary>按同一坐标系平移矩形。</summary>
    public UIRect Offset(vector2 delta) => new(new vector2(min.x + delta.x, min.y + delta.y), size);
}
