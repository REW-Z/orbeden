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
}
#pragma warning restore CS0649

/// <summary>EditorScene Handle 绘制 API。</summary>
public static unsafe class Gizmos
{
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
