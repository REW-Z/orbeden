using System;

namespace Orbeden;

/// <summary>
/// 派生位置的批量写入目标。布局只负责算出结果，写入原生 Transform 的动作由此接口承担，
/// 由原生 UI 上下文实现；未接入时布局结果仍保存在组件上，只是不驱动世界矩阵。
/// </summary>
public interface IUIDerivedPositionSink
{
    /// <summary>一次批量写入派生位置；clear 非零的条目清除该 Ens 的覆盖。</summary>
    /// <returns>全部条目都被接受时返回真。</returns>
    bool ApplyDerivedPositions(ReadOnlySpan<UIDerivedPosition> positions);
}
