namespace Orbeden;

/// <summary>
/// 参与布局测量的节点。实现者给出自身期望尺寸，不修改别人的配置。
/// 布局只读取期望尺寸，因此测量阶段必须只依赖已确定的上阶段数据。
/// </summary>
public interface IUILayoutMeasure
{
    /// <summary>测量期望宽度；不受可用宽度影响。</summary>
    float MeasureWidth();

    /// <summary>测量给定可用宽度下的期望高度。宽度相关的高度（如自动换行的文字）在这里返回。</summary>
    float MeasureHeight(float availableWidth);
}
