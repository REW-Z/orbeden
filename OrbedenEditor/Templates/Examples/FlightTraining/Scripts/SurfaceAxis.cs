using Orbeden;

namespace AviatorSage;

/// <summary>翼面偏转使用的本地轴。</summary>
public enum SurfaceAxis
{
    X,
    Y,
    Z,
}

/// <summary>按选定本地轴叠加翼面偏转角。</summary>
public static class SurfaceAxisEuler
{
    /// <summary>返回叠加偏转后的本地欧拉角。</summary>
    public static vector3 Apply(vector3 euler, SurfaceAxis axis, float angle)
    {
        return axis switch
        {
            SurfaceAxis.X => new vector3(euler.x + angle, euler.y, euler.z),
            SurfaceAxis.Y => new vector3(euler.x, euler.y + angle, euler.z),
            _ => new vector3(euler.x, euler.y, euler.z + angle),
        };
    }
}
