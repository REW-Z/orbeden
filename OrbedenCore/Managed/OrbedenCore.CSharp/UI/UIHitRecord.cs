namespace Orbeden;

/// <summary>
/// 一条命中记录。命中列表与绘制遍历同源，按绘制顺序排列，最后一条在最上层；
/// 命中检测从后往前找第一个 raycastTarget 的图形。
/// </summary>
public readonly struct UIHitRecord
{
    /// <summary>命中的图形。</summary>
    public readonly UIVisual visual;

    /// <summary>图形所属画布。</summary>
    public readonly Canvas canvas;

    /// <summary>图形在自身局部空间的解析矩形。</summary>
    public readonly UIRect rect;

    /// <summary>记录产生时的视图标识。</summary>
    public readonly ulong viewId;

    /// <summary>记录产生时的裁剪层快照；判命中前先用它过滤点。</summary>
    public readonly UIClipSnapshot clips;

    /// <summary>创建命中记录。</summary>
    public UIHitRecord(UIVisual visual, Canvas canvas, UIRect rect, ulong viewId,
        UIClipSnapshot clips)
    {
        this.visual = visual;
        this.canvas = canvas;
        this.rect = rect;
        this.viewId = viewId;
        this.clips = clips;
    }
}
