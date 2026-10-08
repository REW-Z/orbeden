using System.Runtime.InteropServices;

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
