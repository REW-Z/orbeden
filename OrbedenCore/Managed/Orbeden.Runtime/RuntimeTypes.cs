using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>Ens 运行时句柄，布局需要与 C++ EnsId 一致。</summary>
[StructLayout(LayoutKind.Sequential)]
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

/// <summary>三维向量，布局需要与 C++ 托管桥接结构一致。</summary>
[StructLayout(LayoutKind.Sequential)]
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

/// <summary>四元数，布局需要与 C++ quaternion 一致。</summary>
[StructLayout(LayoutKind.Sequential)]
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
[StructLayout(LayoutKind.Sequential)]
public struct color4
{
    public float r;
    public float g;
    public float b;
    public float a;

    /// <summary>创建线性颜色。</summary>
    public color4(float r, float g, float b, float a = 1.0f)
    {
        this.r = r;
        this.g = g;
        this.b = b;
        this.a = a;
    }
}
