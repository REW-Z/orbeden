using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 4)]
internal struct EditorGizmoEdit
{
    public EnsId Ens;
    public int Mode;
    public vector3 StartPosition;
    public quaternion StartRotation;
    public vector3 StartScale;
    public vector3 EndPosition;
    public quaternion EndRotation;
    public vector3 EndScale;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorGizmoApi
{
    public delegate* unmanaged[Cdecl]<vector3, vector3, color, void> Line3D;
    public delegate* unmanaged[Cdecl]<vector3, byte*, int, void> Label3D;
    public delegate* unmanaged[Cdecl]<EditorGizmoEdit*, int> TakeEdit;
    public delegate* unmanaged[Cdecl]<EnsId, byte> IsSelected;
    public delegate* unmanaged[Cdecl]<byte> IsVisible;
}
#pragma warning restore CS0649

/// <summary>EditorScene Handle 绘制 API。</summary>
public static unsafe class Gizmos
{
    public static bool Visible => initialized && api.IsVisible != null && api.IsVisible() != 0;

    /// <summary>查询对象是否在 SceneView 中选中。</summary>
    public static bool IsSelected(EnsId ens) => initialized && api.IsSelected != null && api.IsSelected(ens) != 0;

    /// <summary>绘制由三个大圆组成的球体线框。</summary>
    public static void WireSphere(vector3 center, float radius, color color)
    {
        if (!float.IsFinite(radius) || radius <= 0) return;
        for (int plane = 0; plane < 3; ++plane)
        for (int segment = 0; segment < 48; ++segment)
        {
            float a = segment * MathF.Tau / 48, b = (segment + 1) * MathF.Tau / 48;
            float ax = radius * MathF.Cos(a), ay = radius * MathF.Sin(a);
            float bx = radius * MathF.Cos(b), by = radius * MathF.Sin(b);
            vector3 from = plane == 0 ? new(center.x + ax, center.y + ay, center.z)
                : plane == 1 ? new(center.x + ax, center.y, center.z + ay) : new(center.x, center.y + ax, center.z + ay);
            vector3 to = plane == 0 ? new(center.x + bx, center.y + by, center.z)
                : plane == 1 ? new(center.x + bx, center.y, center.z + by) : new(center.x, center.y + bx, center.z + by);
            Line(from, to, color);
        }
    }

    /// <summary>绘制可旋转盒体线框，size 为完整尺寸。</summary>
    public static void WireCube(vector3 center, vector3 size, color color, quaternion? rotation = null)
    {
        quaternion value = rotation ?? new quaternion(0, 0, 0, 1);
        System.Numerics.Quaternion q = new(value.x, value.y, value.z, value.w);
        q = q.LengthSquared() > 0.000001f ? System.Numerics.Quaternion.Normalize(q) : System.Numerics.Quaternion.Identity;
        Span<vector3> corners = stackalloc vector3[8];
        for (int index = 0; index < 8; ++index)
        {
            System.Numerics.Vector3 local = new((index & 1) == 0 ? -size.x * 0.5f : size.x * 0.5f,
                (index & 2) == 0 ? -size.y * 0.5f : size.y * 0.5f, (index & 4) == 0 ? -size.z * 0.5f : size.z * 0.5f);
            var point = System.Numerics.Vector3.Transform(local, q);
            corners[index] = new(center.x + point.X, center.y + point.Y, center.z + point.Z);
        }
        for (int index = 0; index < 8; ++index)
            for (int bit = 1; bit <= 4; bit <<= 1)
                if ((index & bit) == 0) Line(corners[index], corners[index | bit], color);
    }

    private static EditorGizmoApi api;
    private static bool initialized;

    //保存 C++ 传入的 Gizmo 函数表
    internal static void Initialize(EditorGizmoApi value)
    {
        api = value;
        initialized = api.Line3D != null;
    }

    /// <summary>绘制三维线段。</summary>
    public static void Line(vector3 a, vector3 b, color color)
    {
        if (!initialized || api.Line3D == null) return;
        api.Line3D(a, b, color);
    }

    /// <summary>绘制三维文本标签。</summary>
    public static void Label(vector3 position, string text)
    {
        if (!initialized || api.Label3D == null) return;

        string value = text ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            api.Label3D(position, pointer, byteCount);
        }
    }

    /// <summary>取出一条待提交的手柄编辑，没有时返回 false。</summary>
    internal static bool TakeEdit(out EditorGizmoEdit edit)
    {
        edit = default;
        if (!initialized || api.TakeEdit == null) return false;

        EditorGizmoEdit value = default;
        if (api.TakeEdit(&value) == 0) return false;

        edit = value;
        return true;
    }
}
