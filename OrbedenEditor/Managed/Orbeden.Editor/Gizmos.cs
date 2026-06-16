using System;
using System.Runtime.InteropServices;
using System.Text;
using Orbeden;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct EditorGizmoApi
{
    public delegate* unmanaged<Vector3, Vector3, Color, void> Line3D;
    public delegate* unmanaged<Vector3, byte*, int, void> Label3D;
}
#pragma warning restore CS0649

/// <summary>SceneView Gizmo 绘制 API。</summary>
public static unsafe class Gizmos
{
    private static EditorGizmoApi api;
    private static bool initialized;

    //保存 C++ 传入的 Gizmo 函数表
    internal static void Initialize(IntPtr apiPointer)
    {
        if (apiPointer == IntPtr.Zero)
        {
            initialized = false;
            api = default;
            return;
        }

        api = *(EditorGizmoApi*)apiPointer;
        initialized = true;
    }

    /// <summary>绘制三维线段。</summary>
    public static void Line(Vector3 a, Vector3 b, Color color)
    {
        if (!initialized || api.Line3D == null) return;
        api.Line3D(a, b, color);
    }

    /// <summary>绘制三维文本标签。</summary>
    public static void Label(Vector3 position, string text)
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
}
