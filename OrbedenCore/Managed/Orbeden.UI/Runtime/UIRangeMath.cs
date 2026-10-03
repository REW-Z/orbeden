using System;

namespace Orbeden;

/// <summary>
/// 区间与归一化的纯计算。滑条、滚动条与滚动盒共用同一套公式，
/// 抽成静态方法之后可以脱离宿主单测。
/// </summary>
public static class UIRangeMath
{
    /// <summary>
    /// 把区间内的值归一化到 [0,1]。零区间（maximum ≤ minimum）恒为 0，
    /// 越界值夹到边界，非有限值按 0 处理。
    /// </summary>
    public static float Normalize(float value, float minimum, float maximum)
    {
        if (!float.IsFinite(value) || !float.IsFinite(minimum) || !float.IsFinite(maximum)) return 0.0f;
        float span = maximum - minimum;
        if (span <= 0.0f) return 0.0f;
        return Math.Clamp((value - minimum) / span, 0.0f, 1.0f);
    }

    /// <summary>把 [0,1] 的归一值还原成区间内的值。零区间固定返回 minimum。</summary>
    public static float Denormalize(float normalized, float minimum, float maximum)
    {
        if (!float.IsFinite(normalized) || !float.IsFinite(minimum) || !float.IsFinite(maximum)) return minimum;
        float span = maximum - minimum;
        if (span <= 0.0f) return minimum;
        return minimum + Math.Clamp(normalized, 0.0f, 1.0f) * span;
    }

    /// <summary>整数模式下把值舍入到整数：中点远离零，避免 .5 总是向下。</summary>
    public static float RoundToWhole(float value) => MathF.Round(value, MidpointRounding.AwayFromZero);

    /// <summary>把值夹紧到区间；零区间固定返回 minimum。</summary>
    public static float Clamp(float value, float minimum, float maximum)
    {
        if (!float.IsFinite(value)) return minimum;
        float span = maximum - minimum;
        if (span <= 0.0f) return minimum;
        return Math.Clamp(value, minimum, maximum);
    }
}
