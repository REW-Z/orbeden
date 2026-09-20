using Orbeden;

namespace OrbedenEditor;

/// <summary>批量矩形绘制的共用缓冲操作。</summary>
internal static class EditorRects
{
    /// <summary>往批量缓冲里追加一个实心矩形，返回新的数量；缓冲区满时原样返回。</summary>
    internal static int Append(EditorRectPrimitive[] rects, int count,
        float minX, float minY, float maxX, float maxY, color fill)
    {
        if (count >= rects.Length) return count;

        rects[count].MinX = minX;
        rects[count].MinY = minY;
        rects[count].MaxX = maxX;
        rects[count].MaxY = maxY;
        rects[count].R = fill.r;
        rects[count].G = fill.g;
        rects[count].B = fill.b;
        rects[count].A = fill.a;
        rects[count].Rounding = 0.0f;
        return count + 1;
    }
}
