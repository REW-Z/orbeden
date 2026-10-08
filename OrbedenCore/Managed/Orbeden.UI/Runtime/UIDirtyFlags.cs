using System;

namespace Orbeden;

/// <summary>UI 重建脏类别。位可组合，重建按类别分派，避免一处改动牵动全部缓存。</summary>
[Flags]
public enum UIDirtyFlags
{
    /// <summary>没有待重建内容。</summary>
    None = 0,

    /// <summary>层级或活动状态变化，需要重新同步节点索引。</summary>
    Hierarchy = 1,

    /// <summary>布局尺寸或驱动关系变化。</summary>
    Layout = 2,

    /// <summary>解析位置变化，需要重算世界矩阵。</summary>
    Transform = 4,

    /// <summary>顶点或索引内容变化。</summary>
    Geometry = 8,

    /// <summary>材质、纹理或绘制状态变化。</summary>
    Material = 16,

    /// <summary>裁剪栈内容变化。</summary>
    Clip = 32,

    /// <summary>命中快照失效。</summary>
    Input = 64,
}
