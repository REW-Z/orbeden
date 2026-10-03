using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// Ens 在 UI 树中的托管索引。父子身份来自 Ens 层级，不提供第二套 reparent API。
/// 一个节点可以挂多个 UIElement（例如 Image 与 Button 同节点），按挂载顺序保存。
/// </summary>
public sealed class UINode
{
    /// <summary>节点所属 Ens。</summary>
    public EnsId Ens { get; }

    /// <summary>本节点上的布局组件；没有 UILayout 的节点不参与布局计算。</summary>
    public UILayout? Layout { get; internal set; }

    /// <summary>本节点上的画布；非画布节点为空。</summary>
    public Canvas? Canvas { get; internal set; }

    /// <summary>本节点上的全部 UIElement，按挂载顺序。</summary>
    public List<UIElement> Elements { get; } = [];

    /// <summary>直接子节点，按 Ens 层级顺序。</summary>
    public List<UINode> Children { get; } = [];

    /// <summary>最近的一个 UI 祖先节点；没有时为空，表示这是某棵 UI 子树的根。</summary>
    public UINode? Parent { get; internal set; }

    /// <summary>配置错误描述；非空时该节点不参与布局与提交。</summary>
    public string ConfigurationError { get; internal set; } = string.Empty;

    /// <summary>绘制层次：0 为普通内容，1 为弹层。弹层整体排在普通内容之后。</summary>
    public int Layer { get; internal set; }

    internal UINode(EnsId ens)
    {
        Ens = ens;
    }

    /// <summary>本节点是否可用于渲染：有布局组件且没有配置错误。</summary>
    public bool IsRenderable => ConfigurationError.Length == 0 && Layout != null;

    /// <summary>取本节点上第一个指定类型的 UIElement。</summary>
    public T? GetElement<T>() where T : class
    {
        foreach (UIElement element in Elements)
        {
            if (element is T match) return match;
        }
        return null;
    }

    /// <summary>本节点上的第一个可绘制图形；没有时为空。</summary>
    public UIVisual? Visual => GetElement<UIVisual>();

    /// <summary>测量来源：同节点的容器优先于图形；都没有时为空，由调用方退回 sizeDelta。</summary>
    public IUILayoutMeasure? GetMeasureSource() => GetElement<UILayoutGroup>() ?? GetElement<IUILayoutMeasure>();

    /// <summary>本节点到画布的绘制深度；根为 0。</summary>
    public int Depth
    {
        get
        {
            int depth = 0;
            for (UINode? current = Parent; current != null; current = current.Parent) ++depth;
            return depth;
        }
    }
}
