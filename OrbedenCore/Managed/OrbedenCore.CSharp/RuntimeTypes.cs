using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Intrinsics;
using NumericsMatrix4x4 = System.Numerics.Matrix4x4;

namespace Orbeden;

/// <summary>Ens 运行时句柄，布局需要与 C++ EnsId 一致。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct EnsId : IEquatable<EnsId>
{
    /// <summary>空 Ens 句柄。</summary>
    public static readonly EnsId Null = new(uint.MaxValue, 0);

    public uint id;
    public uint version;

    /// <summary>创建 Ens 运行时句柄。</summary>
    public EnsId(uint id, uint version)
    {
        this.id = id;
        this.version = version;
    }

    /// <summary>判断句柄是否为空。</summary>
    public readonly bool IsNull => id == uint.MaxValue;

    /// <summary>判断两个句柄是否相同。</summary>
    public readonly bool Equals(EnsId other)
    {
        return id == other.id && version == other.version;
    }

    /// <summary>判断两个句柄是否相同。</summary>
    public override readonly bool Equals(object? obj)
    {
        return obj is EnsId other && Equals(other);
    }

    /// <summary>获取哈希值。</summary>
    public override readonly int GetHashCode()
    {
        return HashCode.Combine(id, version);
    }
}

public partial struct MeshInstanceData
{
    /// <summary>在指定世界坐标创建实例，使用单位旋转、单位缩放、白色和完整 UV 区域。</summary>
    public MeshInstanceData(vector3 position)
    {
        this.position = position;
        rotation = new(0, 0, 0, 1);
        scale = new(1, 1, 1);
        tint = new(1, 1, 1, 1);
        uvRect = new(0, 0, 1, 1);
    }
}

public partial struct InstanceDrawOptions
{
    /// <summary>默认绘制到第一个图层，对全部相机可见，并投射和接收阴影。</summary>
    public static InstanceDrawOptions Default => new()
    {
        drawLayer = 1, castShadows = true, receiveShadows = true, camera = EnsId.Null
    };
}

/// <summary>二维向量，布局需要与 C++ 托管桥接结构一致。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct vector2
{
    public float x;
    public float y;

    /// <summary>创建二维向量。</summary>
    public vector2(float x, float y)
    {
        this.x = x;
        this.y = y;
    }
}


/// <summary>三维向量，布局需要与 C++ 托管桥接结构一致。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct vector3
{
    public float x;
    public float y;
    public float z;

    /// <summary>创建三维向量。</summary>
    public vector3(float x, float y, float z)
    {
        this.x = x;
        this.y = y;
        this.z = z;
    }
}

/// <summary>列主序 4×4 矩阵，布局需要与 C++ matrix4x4 一致（16 个连续 float）。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public unsafe struct matrix4x4
{
    /// <summary>列主序的 16 个分量。</summary>
    public fixed float m[16];

    /// <summary>单位矩阵。</summary>
    public static matrix4x4 Identity
    {
        get
        {
            matrix4x4 result = default;
            result.m[0] = 1.0f;
            result.m[5] = 1.0f;
            result.m[10] = 1.0f;
            result.m[15] = 1.0f;
            return result;
        }
    }

    /// <summary>按列主序下标读写分量。</summary>
    public float this[int index]
    {
        readonly get => m[index];
        set => m[index] = value;
    }

    /// <summary>列主序矩阵相乘：结果等价于先施加 right、再施加 left。</summary>
    public static matrix4x4 Multiply(in matrix4x4 left, in matrix4x4 right)
    {
        matrix4x4 result = default;
        if (Vector128.IsHardwareAccelerated)
        {
            //按列并行累加四行，沿用标量乘加顺序
            fixed (float* columns = left.m)
            {
                Vector128<float> left0 = Vector128.Load(columns);
                Vector128<float> left1 = Vector128.Load(columns + 4);
                Vector128<float> left2 = Vector128.Load(columns + 8);
                Vector128<float> left3 = Vector128.Load(columns + 12);
                for (int column = 0; column < 16; column += 4)
                {
                    Vector128<float> sum = Vector128<float>.Zero;
                    sum += left0 * Vector128.Create(right.m[column]);
                    sum += left1 * Vector128.Create(right.m[column + 1]);
                    sum += left2 * Vector128.Create(right.m[column + 2]);
                    sum += left3 * Vector128.Create(right.m[column + 3]);
                    sum.Store(result.m + column);
                }
            }
            return result;
        }

        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += left.m[k * 4 + row] * right.m[column * 4 + k];
                result.m[column * 4 + row] = sum;
            }
        }
        return result;
    }

    /// <summary>构造平移、旋转、缩放复合的局部矩阵。</summary>
    public static matrix4x4 Trs(in vector3 translation, in quaternion rotation, in vector3 scale)
    {
        float x = rotation.x, y = rotation.y, z = rotation.z, w = rotation.w;
        //四元数转列主序 3×3，再按列乘上缩放。
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

#if !DEBUG
        if (Vector128.IsHardwareAccelerated)
        {
            //按列缩放旋转基向量并写入平移
            matrix4x4 scaled = default;
            (Vector128.Create(1f - 2f * (yy + zz), 2f * (xy + wz), 2f * (xz - wy), 0f) * Vector128.Create(scale.x).WithElement(3, 1f)).Store(scaled.m);
            (Vector128.Create(2f * (xy - wz), 1f - 2f * (xx + zz), 2f * (yz + wx), 0f) * Vector128.Create(scale.y).WithElement(3, 1f)).Store(scaled.m + 4);
            (Vector128.Create(2f * (xz + wy), 2f * (yz - wx), 1f - 2f * (xx + yy), 0f) * Vector128.Create(scale.z).WithElement(3, 1f)).Store(scaled.m + 8);
            Vector128.Create(translation.x, translation.y, translation.z, 1f).Store(scaled.m + 12);
            return scaled;
        }
#endif

        matrix4x4 result = Identity;
        result.m[0] = (1.0f - 2.0f * (yy + zz)) * scale.x;
        result.m[1] = (2.0f * (xy + wz)) * scale.x;
        result.m[2] = (2.0f * (xz - wy)) * scale.x;

        result.m[4] = (2.0f * (xy - wz)) * scale.y;
        result.m[5] = (1.0f - 2.0f * (xx + zz)) * scale.y;
        result.m[6] = (2.0f * (yz + wx)) * scale.y;

        result.m[8] = (2.0f * (xz + wy)) * scale.z;
        result.m[9] = (2.0f * (yz - wx)) * scale.z;
        result.m[10] = (1.0f - 2.0f * (xx + yy)) * scale.z;

        result.m[12] = translation.x;
        result.m[13] = translation.y;
        result.m[14] = translation.z;
        return result;
    }

    /// <summary>节点到画布根的局部矩阵连乘；画布根本身不参与投影。</summary>
    internal static matrix4x4 Compose(UINode node)
    {
        if (UIWorldContext.Current?.RefreshTransformMatrices() == true)
        {
            return node.CachedCanvasMatrix;
        }

        matrix4x4 result = Local(node);
        for (UINode? current = node.Parent; current != null; current = current.Parent)
        {
            if (current.Canvas != null)
            {
                break;
            }
            result = matrix4x4.Multiply(Local(current), result);
        }
        return result;
    }

    /// <summary>节点到世界空间的矩阵：连画布节点自己的变换一起乘进去，世界空间命中要用它。</summary>
    internal static matrix4x4 ComposeIncludingCanvas(UINode node)
    {
        if (UIWorldContext.Current?.RefreshTransformMatrices() == true)
        {
            return node.CachedWorldMatrix;
        }

        matrix4x4 result = Local(node);
        UINode root = node;
        for (UINode? current = node.Parent; current != null; current = current.Parent)
        {
            result = matrix4x4.Multiply(Local(current), result);
            root = current;
        }
        //叠加画布外部父级变换
        EnsId parent = root.OwnerTransform.GetParent();
        while (!parent.IsNull)
        {
            Transform transform = Ens.FromId(parent).Transform;
            matrix4x4 local = matrix4x4.Trs(transform.localPosition, transform.localRotation, transform.localScale);
            result = matrix4x4.Multiply(local, result);
            parent = transform.GetParent();
        }
        return result;
    }

    /// <summary>使用布局合成位置，旋转与缩放取作者值。</summary>
    internal static matrix4x4 Local(UINode node)
    {
        Transform transform = node.OwnerTransform;
        vector3 position = node.Layout?.GetResolvedLocalPosition() ?? transform.localPosition;
        return matrix4x4.Trs(position, transform.localRotation, transform.localScale);
    }

    /// <summary>
    /// 构造屏幕画布投影，把 XY 映射到 [-1,1]，最终裁剪深度固定为零。
    /// </summary>
    public static matrix4x4 Ortho(float left, float right, float bottom, float top)
    {
        matrix4x4 result = matrix4x4.Identity;
        float width = right - left;
        float height = top - bottom;
        if (width == 0.0f || height == 0.0f) return result;

        result[0] = 2.0f / width;
        result[5] = 2.0f / height;
        result[10] = 0.0f;
        result[12] = -(right + left) / width;
        result[13] = -(top + bottom) / height;
        return result;
    }

    /// <summary>
    /// 通用 4×4 求逆，按列主序；奇异时返回假。
    /// 投影矩阵不是仿射矩阵（最后一行带着 -1/w），反投影必须用它而不是 TryInvertAffine。
    /// </summary>
    public static bool TryInvert(in matrix4x4 matrix, out matrix4x4 inverse)
    {
        inverse = matrix4x4.Identity;
        if (Vector128.IsHardwareAccelerated)
        {
            return TryInvertSimd(matrix, out inverse);
        }

        //按行主序的中间副本做余子式展开，写回时再转回列主序。
        Span<float> a = stackalloc float[16];
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row) a[row * 4 + column] = matrix[column * 4 + row];
        }

        Span<float> inv = stackalloc float[16];
        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15]
            + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15]
            - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15]
            + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14]
            - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15]
            - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15]
            + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15]
            - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14]
            + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15]
            + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15]
            - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15]
            + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14]
            - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11]
            - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11]
            + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11]
            - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10]
            + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

        float determinant = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (!float.IsFinite(determinant) || MathF.Abs(determinant) < 1e-12f) return false;

        float scale = 1.0f / determinant;
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row) inverse[column * 4 + row] = inv[row * 4 + column] * scale;
        }
        return true;
    }

    //并行计算伴随矩阵并按原有阈值判断是否可逆
    private static bool TryInvertSimd(in matrix4x4 matrix, out matrix4x4 inverse)
    {
        fixed (float* columns = matrix.m)
        {
            Vector128<float> first = Vector128.Load(columns);
            Vector128<float> second = Vector128.Load(columns + 4);
            Vector128<float> third = Vector128.Load(columns + 8);
            Vector128<float> fourth = Vector128.Load(columns + 12);
            Vector128<float> evenSigns = Vector128.Create(0f, -0f, 0f, -0f);
            Vector128<float> oddSigns = Vector128.Create(-0f, 0f, -0f, 0f);

            Vector128<float> c0 = CalculateCofactors(second, third, fourth, evenSigns);
            Vector128<float> c1 = CalculateCofactors(first, third, fourth, oddSigns);
            Vector128<float> c2 = CalculateCofactors(first, second, fourth, evenSigns);
            Vector128<float> c3 = CalculateCofactors(first, second, third, oddSigns);
            float determinant = columns[0] * c0.ToScalar() + columns[4] * c1.ToScalar()
                + columns[8] * c2.ToScalar() + columns[12] * c3.ToScalar();
            if (!float.IsFinite(determinant) || MathF.Abs(determinant) < 1e-12f)
            {
                inverse = Identity;
                return false;
            }

            //缩放余子式并转置回列主序
            Vector128<float> scale = Vector128.Create(1.0f / determinant);
            matrix4x4 result = default;
            (c0 * scale).Store(result.m);
            (c1 * scale).Store(result.m + 4);
            (c2 * scale).Store(result.m + 8);
            (c3 * scale).Store(result.m + 12);
            NumericsMatrix4x4 rows = Unsafe.BitCast<matrix4x4, NumericsMatrix4x4>(result);
            inverse = Unsafe.BitCast<NumericsMatrix4x4, matrix4x4>(NumericsMatrix4x4.Transpose(rows));
            return true;
        }
    }

    //保留标量乘积与累加顺序，同时计算四个余子式
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static Vector128<float> CalculateCofactors(Vector128<float> first, Vector128<float> second, Vector128<float> third, Vector128<float> signs)
    {
        Vector128<int> low = Vector128.Create(1, 0, 0, 0);
        Vector128<int> middle = Vector128.Create(2, 2, 1, 1);
        Vector128<int> high = Vector128.Create(3, 3, 3, 2);
        Vector128<float> a = Vector128.Shuffle(first, low);
        Vector128<float> b = Vector128.Shuffle(first, middle);
        Vector128<float> c = Vector128.Shuffle(first, high);
        Vector128<float> d = Vector128.Shuffle(second, low);
        Vector128<float> e = Vector128.Shuffle(second, middle);
        Vector128<float> f = Vector128.Shuffle(second, high);
        Vector128<float> g = Vector128.Shuffle(third, low);
        Vector128<float> h = Vector128.Shuffle(third, middle);
        Vector128<float> i = Vector128.Shuffle(third, high);

        Vector128<float> result = (a ^ signs) * e * i;
        result -= (a * h * f) ^ signs;
        result -= (b * d * i) ^ signs;
        result += (b * g * f) ^ signs;
        result += (c * d * h) ^ signs;
        return result - ((c * g * e) ^ signs);
    }

    /// <summary>按通用矩阵变换齐次点并做 w 归一；w 接近零时返回假。</summary>
    public static bool TryTransformHomogeneous(in matrix4x4 matrix, in vector3 point, out vector3 result)
    {
        if (Vector128.IsHardwareAccelerated)
        {
            //同时变换四个齐次坐标分量
            fixed (float* columns = matrix.m)
            {
                Vector128<float> transformed = Vector128.Load(columns) * Vector128.Create(point.x);
                transformed += Vector128.Load(columns + 4) * Vector128.Create(point.y);
                transformed += Vector128.Load(columns + 8) * Vector128.Create(point.z);
                transformed += Vector128.Load(columns + 12);
                float divisor = transformed.GetElement(3);
                if (!float.IsFinite(divisor) || MathF.Abs(divisor) < 1e-8f)
                {
                    result = default;
                    return false;
                }
                Vector128<float> normalized = transformed / Vector128.Create(divisor);
                result = new vector3(normalized.GetElement(0), normalized.GetElement(1), normalized.GetElement(2));
                return true;
            }
        }

        float x = matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12];
        float y = matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13];
        float z = matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14];
        float w = matrix[3] * point.x + matrix[7] * point.y + matrix[11] * point.z + matrix[15];
        if (!float.IsFinite(w) || MathF.Abs(w) < 1e-8f)
        {
            result = default;
            return false;
        }
        result = new vector3(x / w, y / w, z / w);
        return true;
    }

    /// <summary>把一个点按列主序矩阵变换到目标空间；z 按零处理。</summary>
    public static vector2 TransformPoint(in matrix4x4 matrix, vector2 point) => new(
        matrix[0] * point.x + matrix[4] * point.y + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[13]);

    /// <summary>
    /// 求仿射矩阵的逆；平移与线性部分分开处理。线性部分奇异时返回假，
    /// 调用方按不可见、不可命中处理。
    /// </summary>
    public static bool TryInvertAffine(in matrix4x4 matrix, out matrix4x4 inverse)
    {
        inverse = matrix4x4.Identity;
        //列主序 3×3：列向量是 m[0..2]、m[4..6]、m[8..10]。
        float a = matrix[0], b = matrix[1], c = matrix[2];
        float d = matrix[4], e = matrix[5], f = matrix[6];
        float g = matrix[8], h = matrix[9], i = matrix[10];

        float determinant = a * (e * i - f * h) - d * (b * i - c * h) + g * (b * f - c * e);
        if (!float.IsFinite(determinant) || MathF.Abs(determinant) < 1e-12f) return false;

        float inv = 1.0f / determinant;
        //伴随矩阵转置后直接落到列主序的位置上。
        inverse[0] = (e * i - f * h) * inv;
        inverse[1] = (c * h - b * i) * inv;
        inverse[2] = (b * f - c * e) * inv;
        inverse[4] = (f * g - d * i) * inv;
        inverse[5] = (a * i - c * g) * inv;
        inverse[6] = (c * d - a * f) * inv;
        inverse[8] = (d * h - e * g) * inv;
        inverse[9] = (b * g - a * h) * inv;
        inverse[10] = (a * e - b * d) * inv;

        float tx = matrix[12], ty = matrix[13], tz = matrix[14];
        inverse[12] = -(inverse[0] * tx + inverse[4] * ty + inverse[8] * tz);
        inverse[13] = -(inverse[1] * tx + inverse[5] * ty + inverse[9] * tz);
        inverse[14] = -(inverse[2] * tx + inverse[6] * ty + inverse[10] * tz);
        return true;
    }
}

/// <summary>四元数，布局需要与 C++ quaternion 一致。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct quaternion
{
    public float x;
    public float y;
    public float z;
    public float w;

    /// <summary>创建四元数。</summary>
    public quaternion(float x, float y, float z, float w)
    {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }
}

/// <summary>线性颜色，布局需要与 C++ 托管桥接结构一致。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct color
{
    public float r;
    public float g;
    public float b;
    public float a;

    /// <summary>创建线性颜色。</summary>
    public color(float r, float g, float b, float a = 1.0f)
    {
        this.r = r;
        this.g = g;
        this.b = b;
        this.a = a;
    }
}
