using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>Ens 运行时句柄，布局需要与 C++ EnsId 一致。</summary>
[StructLayout(LayoutKind.Sequential)]
public struct EnsId
{
    public uint id;
    public uint version;

    /// <summary>创建 Ens 运行时句柄。</summary>
    public EnsId(uint id, uint version)
    {
        this.id = id;
        this.version = version;
    }
}

/// <summary>三维向量，布局需要与 C++ 托管桥接结构一致。</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vector3
{
    public float x;
    public float y;
    public float z;

    /// <summary>创建三维向量。</summary>
    public Vector3(float x, float y, float z)
    {
        this.x = x;
        this.y = y;
        this.z = z;
    }
}

/// <summary>线性颜色，布局需要与 C++ 托管桥接结构一致。</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Color
{
    public float r;
    public float g;
    public float b;
    public float a;

    /// <summary>创建线性颜色。</summary>
    public Color(float r, float g, float b, float a = 1.0f)
    {
        this.r = r;
        this.g = g;
        this.b = b;
        this.a = a;
    }
}
