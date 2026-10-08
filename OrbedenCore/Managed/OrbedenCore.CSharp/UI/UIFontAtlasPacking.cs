namespace Orbeden;

/// <summary>
/// Atlas 页的逐行装箱游标。不引用纹理与原生资源，装箱规则可以脱离宿主单独验证：
/// 行内放不下就换行，页内放不下就报失败交给上层换页，已有字形永不移动。
/// </summary>
public struct UIFontAtlasCursor
{
    /// <summary>当前行已经用到的宽度。</summary>
    public int cursorX;

    /// <summary>当前行的顶部。</summary>
    public int cursorY;

    /// <summary>当前行的行高，换行时按它下移。</summary>
    public int rowHeight;

    /// <summary>
    /// 在给定页尺寸上分配一块 width×height 的区域。成功时返回左下角并推进游标；
    /// 失败时游标保持不变，调用方改用新页。
    /// </summary>
    public bool TryAllocate(int pageWidth, int pageHeight, int width, int height, out int x, out int y)
    {
        x = 0;
        y = 0;
        if (width <= 0 || height <= 0 || pageWidth <= 0 || pageHeight <= 0) return false;
        //比整页还大的块永远放不下，换行也救不了，直接拒绝交给上层换独占页。
        if (width > pageWidth || height > pageHeight) return false;

        int nextX = cursorX;
        int nextY = cursorY;
        int nextRowHeight = rowHeight;
        if (nextX + width > pageWidth)
        {
            nextX = 0;
            nextY += nextRowHeight;
            nextRowHeight = 0;
        }
        //页内放不下：游标不动，交给上层换页。
        if (nextY + height > pageHeight) return false;

        x = nextX;
        y = nextY;
        cursorX = nextX + width;
        cursorY = nextY;
        rowHeight = nextRowHeight > height ? nextRowHeight : height;
        return true;
    }

    /// <summary>求能容纳给定字形的独占页边长：不小于 minimum 的最小二次幂。</summary>
    public static int DedicatedPageSize(int width, int height, int minimum)
    {
        int size = minimum > 0 ? minimum : 1;
        while (size < width || size < height) size <<= 1;
        return size;
    }
}
