using System.Runtime.CompilerServices;
using System.Runtime.Intrinsics;

namespace Orbeden;

/// <summary>在托管端计算二维和三维向量。</summary>
public static class VectorExtensions
{
    /// <summary>逐分量相加。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Add(this in vector2 left, in vector2 right) => ToVector2(Load(left) + Load(right));

    /// <summary>逐分量相减。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Subtract(this in vector2 left, in vector2 right) => ToVector2(Load(left) - Load(right));

    /// <summary>逐分量相乘。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Multiply(this in vector2 left, in vector2 right) => ToVector2(Load(left) * Load(right));

    /// <summary>按标量缩放向量。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Multiply(this in vector2 value, float scale) => ToVector2(Load(value) * scale);

    /// <summary>逐分量相除。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Divide(this in vector2 left, in vector2 right) => ToVector2(Load(left) / Load(right));

    /// <summary>向量的各分量除以标量。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Divide(this in vector2 value, float divisor) => ToVector2(Load(value) / divisor);

    /// <summary>逐分量取反。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Negate(this in vector2 value) => ToVector2(-Load(value));

    /// <summary>计算向量的点积。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Dot(this in vector2 left, in vector2 right) => Vector128.Dot(Load(left), Load(right));

    /// <summary>计算二维叉积的 Z 分量。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Cross(this in vector2 left, in vector2 right)
    {
        var product = Load(left) * Vector128.Shuffle(Load(right), Vector128.Create(1, 0, 2, 3));
        return product.ToScalar() - product.GetElement(1);
    }

    /// <summary>计算向量长度的平方。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float LengthSquared(this in vector2 value)
    {
        var vector = Load(value);
        return Vector128.Dot(vector, vector);
    }

    /// <summary>计算向量长度。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Length(this in vector2 value) => MathF.Sqrt(value.LengthSquared());

    /// <summary>计算两点距离的平方。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float DistanceSquared(this in vector2 left, in vector2 right)
    {
        var delta = Load(left) - Load(right);
        return Vector128.Dot(delta, delta);
    }

    /// <summary>计算两点距离。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Distance(this in vector2 left, in vector2 right) => MathF.Sqrt(left.DistanceSquared(right));

    /// <summary>除以向量长度得到单位向量，零向量返回 NaN 分量。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Normalize(this in vector2 value)
    {
        var vector = Load(value);
        return ToVector2(vector / MathF.Sqrt(Vector128.Dot(vector, vector)));
    }

    /// <summary>在两个向量之间线性插值，插值系数可超出零到一。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Lerp(this in vector2 from, in vector2 to, float amount) =>
        ToVector2(Vector128.Lerp(Load(from), Load(to), Vector128.Create(amount)));

    /// <summary>逐分量选择较小值。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Min(this in vector2 left, in vector2 right) => ToVector2(Vector128.Min(Load(left), Load(right)));

    /// <summary>逐分量选择较大值。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Max(this in vector2 left, in vector2 right) => ToVector2(Vector128.Max(Load(left), Load(right)));

    /// <summary>将各分量限制在指定范围内。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Clamp(this in vector2 value, in vector2 minimum, in vector2 maximum) =>
        ToVector2(Vector128.Clamp(Load(value), Load(minimum), Load(maximum)));

    /// <summary>逐分量取绝对值。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector2 Abs(this in vector2 value) => ToVector2(Vector128.Abs(Load(value)));

    /// <summary>逐分量相加。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Add(this in vector3 left, in vector3 right) => ToVector3(Load(left) + Load(right));

    /// <summary>逐分量相减。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Subtract(this in vector3 left, in vector3 right) => ToVector3(Load(left) - Load(right));

    /// <summary>逐分量相乘。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Multiply(this in vector3 left, in vector3 right) => ToVector3(Load(left) * Load(right));

    /// <summary>按标量缩放向量。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Multiply(this in vector3 value, float scale) => ToVector3(Load(value) * scale);

    /// <summary>逐分量相除。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Divide(this in vector3 left, in vector3 right) => ToVector3(Load(left) / Load(right));

    /// <summary>向量的各分量除以标量。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Divide(this in vector3 value, float divisor) => ToVector3(Load(value) / divisor);

    /// <summary>逐分量取反。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Negate(this in vector3 value) => ToVector3(-Load(value));

    /// <summary>计算向量的点积。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Dot(this in vector3 left, in vector3 right) => Vector128.Dot(Load(left), Load(right));

    /// <summary>计算右手系下的向量叉积。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Cross(this in vector3 left, in vector3 right)
    {
        var a = Load(left);
        var b = Load(right);
        var yzx = Vector128.Create(1, 2, 0, 3);
        var zxy = a * Vector128.Shuffle(b, yzx) - Vector128.Shuffle(a, yzx) * b;
        return ToVector3(Vector128.Shuffle(zxy, yzx));
    }

    /// <summary>计算向量长度的平方。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float LengthSquared(this in vector3 value)
    {
        var vector = Load(value);
        return Vector128.Dot(vector, vector);
    }

    /// <summary>计算向量长度。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Length(this in vector3 value) => MathF.Sqrt(value.LengthSquared());

    /// <summary>计算两点距离的平方。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float DistanceSquared(this in vector3 left, in vector3 right)
    {
        var delta = Load(left) - Load(right);
        return Vector128.Dot(delta, delta);
    }

    /// <summary>计算两点距离。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Distance(this in vector3 left, in vector3 right) => MathF.Sqrt(left.DistanceSquared(right));

    /// <summary>除以向量长度得到单位向量，零向量返回 NaN 分量。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Normalize(this in vector3 value)
    {
        var vector = Load(value);
        return ToVector3(vector / MathF.Sqrt(Vector128.Dot(vector, vector)));
    }

    /// <summary>在两个向量之间线性插值，插值系数可超出零到一。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Lerp(this in vector3 from, in vector3 to, float amount) =>
        ToVector3(Vector128.Lerp(Load(from), Load(to), Vector128.Create(amount)));

    /// <summary>逐分量选择较小值。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Min(this in vector3 left, in vector3 right) => ToVector3(Vector128.Min(Load(left), Load(right)));

    /// <summary>逐分量选择较大值。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Max(this in vector3 left, in vector3 right) => ToVector3(Vector128.Max(Load(left), Load(right)));

    /// <summary>将各分量限制在指定范围内。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Clamp(this in vector3 value, in vector3 minimum, in vector3 maximum) =>
        ToVector3(Vector128.Clamp(Load(value), Load(minimum), Load(maximum)));

    /// <summary>逐分量取绝对值。</summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static vector3 Abs(this in vector3 value) => ToVector3(Vector128.Abs(Load(value)));

    //将两个连续 float 载入低 64 位
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static Vector128<float> Load(in vector2 value) => Vector128.CreateScalar(Unsafe.BitCast<vector2, ulong>(value)).AsSingle();

    //分别载入 XY 和 Z，总共读取 12 字节
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static Vector128<float> Load(in vector3 value)
    {
        ulong xy = Unsafe.ReadUnaligned<ulong>(in Unsafe.As<float, byte>(ref Unsafe.AsRef(in value.x)));
        return Vector128.CreateScalar(xy).AsSingle().WithElement(2, value.z);
    }

    //取出低 64 位的两个 float
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static vector2 ToVector2(Vector128<float> value) => Unsafe.BitCast<ulong, vector2>(value.AsUInt64().ToScalar());

    //写入 XY 和 Z，总共写入 12 字节
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static vector3 ToVector3(Vector128<float> value)
    {
        Unsafe.SkipInit(out vector3 result);
        Unsafe.WriteUnaligned(ref Unsafe.As<float, byte>(ref result.x), value.AsUInt64().ToScalar());
        result.z = value.GetElement(2);
        return result;
    }
}
