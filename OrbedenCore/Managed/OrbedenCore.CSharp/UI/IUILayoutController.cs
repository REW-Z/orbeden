namespace Orbeden;

/// <summary>
/// 驱动子节点排列的容器。排列只写子节点的驱动矩形，不改动它们的锚点配置。
/// 容器按 layout 阶段的先后顺序被调用：先水平排 X 与宽，再垂直排 Y 与高。
/// </summary>
public interface IUILayoutController
{
    /// <summary>按当前可用宽度排列子节点的水平位置与宽度。</summary>
    void ArrangeHorizontal();

    /// <summary>按当前可用高度排列子节点的垂直位置与高度。</summary>
    void ArrangeVertical();
}
