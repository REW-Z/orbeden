using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

/// <summary>Runtime GUI 自由绘制 API，用于 PFD、仪表等自定义 HUD。</summary>
public static partial class GUI
{
    /// <summary>把 RGBA 颜色打包为原生 ImU32 字节序（0xAABBGGRR）。</summary>
    public static uint Rgba(byte r, byte g, byte b, byte a = 255)
    {
        return (uint)((a << 24) | (b << 16) | (g << 8) | r);
    }

    /// <summary>获取主视口工作区尺寸。</summary>
    public static vector2 GetViewportSize()
    {
        if (!drawInitialized) return default;
        return NativeGetViewportSize();
    }

    /// <summary>开始一个固定位置、无边框、不响应输入的绘制窗口。</summary>
    public static bool BeginFixedWindow(string title, float x, float y, float width, float height)
    {
        if (!drawInitialized) return false;
        return NativeBeginFixedWindow(title, x, y, width, height);
    }

    /// <summary>结束当前固定绘制窗口。</summary>
    public static void EndFixedWindow()
    {
        if (!drawInitialized) return;
        NativeEndFixedWindow();
    }

    /// <summary>绘制线段。</summary>
    public static void Line(float x0, float y0, float x1, float y1, uint color, float thickness = 1.0f)
    {
        if (!drawInitialized) return;
        NativeLine(x0, y0, x1, y1, color, thickness);
    }

    /// <summary>绘制折线。</summary>
    public static void Polyline(vector2[] points, uint color, float thickness = 1.0f, bool closed = false)
    {
        if (!drawInitialized || points == null || points.Length < 2) return;
        NativePolyline(points, color, thickness, closed);
    }

    /// <summary>绘制矩形边框。</summary>
    public static void Rect(float minX, float minY, float maxX, float maxY, uint color, float thickness = 1.0f, float rounding = 0.0f)
    {
        if (!drawInitialized) return;
        NativeRect(minX, minY, maxX, maxY, color, thickness, rounding);
    }

    /// <summary>绘制实心矩形。</summary>
    public static void RectFilled(float minX, float minY, float maxX, float maxY, uint color, float rounding = 0.0f)
    {
        if (!drawInitialized) return;
        NativeRectFilled(minX, minY, maxX, maxY, color, rounding);
    }

    /// <summary>绘制圆形边框。</summary>
    public static void Circle(float cx, float cy, float radius, uint color, float thickness = 1.0f, int segments = 32)
    {
        if (!drawInitialized) return;
        NativeCircle(cx, cy, radius, color, thickness, segments);
    }

    /// <summary>绘制实心圆。</summary>
    public static void CircleFilled(float cx, float cy, float radius, uint color, int segments = 32)
    {
        if (!drawInitialized) return;
        NativeCircleFilled(cx, cy, radius, color, segments);
    }

    /// <summary>绘制圆弧，角度为弧度。</summary>
    public static void Arc(float cx, float cy, float radius, float minAngleRad, float maxAngleRad, uint color, float thickness = 1.0f, int segments = 24)
    {
        if (!drawInitialized) return;
        NativeArc(cx, cy, radius, minAngleRad, maxAngleRad, color, thickness, segments);
    }

    /// <summary>绘制实心三角形。</summary>
    public static void TriangleFilled(float x0, float y0, float x1, float y1, float x2, float y2, uint color)
    {
        if (!drawInitialized) return;
        NativeTriangleFilled(x0, y0, x1, y1, x2, y2, color);
    }

    /// <summary>在指定位置绘制文本，字号按默认字体大小缩放。</summary>
    public static void Text(string text, float x, float y, uint color, float fontScale = 1.0f)
    {
        if (!drawInitialized) return;
        NativeText(text, x, y, color, fontScale);
    }

    /// <summary>获取文本绘制尺寸。</summary>
    public static vector2 GetTextSize(string text, float fontScale = 1.0f)
    {
        if (!drawInitialized) return default;
        return NativeGetTextSize(text, fontScale);
    }

    /// <summary>推入裁剪区域，之后的绘制只在区域内可见。</summary>
    public static void PushClipRect(float minX, float minY, float maxX, float maxY)
    {
        if (!drawInitialized) return;
        NativePushClipRect(minX, minY, maxX, maxY);
    }

    /// <summary>弹出裁剪区域。</summary>
    public static void PopClipRect()
    {
        if (!drawInitialized) return;
        NativePopClipRect();
    }
}

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct RuntimeGuiDrawApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, float, float, float, float, byte> BeginFixedWindow;
    public delegate* unmanaged[Cdecl]<void> EndFixedWindow;
    public delegate* unmanaged[Cdecl]<float*, float*, void> GetViewportSize;
    public delegate* unmanaged[Cdecl]<float, float, float, float, uint, float, void> Line;
    public delegate* unmanaged[Cdecl]<vector2*, int, uint, float, byte, void> Polyline;
    public delegate* unmanaged[Cdecl]<float, float, float, float, uint, float, float, void> Rect;
    public delegate* unmanaged[Cdecl]<float, float, float, float, uint, float, void> RectFilled;
    public delegate* unmanaged[Cdecl]<float, float, float, uint, float, int, void> Circle;
    public delegate* unmanaged[Cdecl]<float, float, float, uint, int, void> CircleFilled;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, uint, float, int, void> Arc;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, uint, void> TriangleFilled;
    public delegate* unmanaged[Cdecl]<byte*, int, float, float, uint, float, void> Text;
    public delegate* unmanaged[Cdecl]<byte*, int, float, float*, float*, void> GetTextSize;
    public delegate* unmanaged[Cdecl]<float, float, float, float, byte, void> PushClipRect;
    public delegate* unmanaged[Cdecl]<void> PopClipRect;
}
#pragma warning restore CS0649

public static unsafe partial class GUI
{
    private static RuntimeGuiDrawApi drawApi;
    private static bool drawInitialized;

    //保存 C++ 传入的自由绘制函数表
    internal static void InitializeDrawApi(RuntimeGuiDrawApi value)
    {
        drawApi = value;
        drawInitialized = drawApi.BeginFixedWindow != null;
    }

    private static byte[] EncodeUtf8(string? text)
    {
        return Encoding.UTF8.GetBytes(text ?? string.Empty);
    }

    internal static bool NativeBeginFixedWindow(string? title, float x, float y, float width, float height)
    {
        if (drawApi.BeginFixedWindow == null) return false;

        byte[] bytes = EncodeUtf8(title);
        fixed (byte* pointer = bytes)
        {
            return drawApi.BeginFixedWindow(pointer, bytes.Length, x, y, width, height) != 0;
        }
    }

    internal static void NativeEndFixedWindow()
    {
        if (drawApi.EndFixedWindow == null) return;
        drawApi.EndFixedWindow();
    }

    internal static vector2 NativeGetViewportSize()
    {
        if (drawApi.GetViewportSize == null) return default;

        float width = 0.0f;
        float height = 0.0f;
        float* widthPointer = &width;
        float* heightPointer = &height;
        drawApi.GetViewportSize(widthPointer, heightPointer);
        return new vector2(width, height);
    }

    internal static void NativeLine(float x0, float y0, float x1, float y1, uint color, float thickness)
    {
        if (drawApi.Line == null) return;
        drawApi.Line(x0, y0, x1, y1, color, thickness);
    }

    internal static void NativePolyline(vector2[] points, uint color, float thickness, bool closed)
    {
        if (drawApi.Polyline == null) return;

        fixed (vector2* pointer = points)
        {
            drawApi.Polyline(pointer, points.Length, color, thickness, closed ? (byte)1 : (byte)0);
        }
    }

    internal static void NativeRect(float minX, float minY, float maxX, float maxY, uint color, float thickness, float rounding)
    {
        if (drawApi.Rect == null) return;
        drawApi.Rect(minX, minY, maxX, maxY, color, thickness, rounding);
    }

    internal static void NativeRectFilled(float minX, float minY, float maxX, float maxY, uint color, float rounding)
    {
        if (drawApi.RectFilled == null) return;
        drawApi.RectFilled(minX, minY, maxX, maxY, color, rounding);
    }

    internal static void NativeCircle(float cx, float cy, float radius, uint color, float thickness, int segments)
    {
        if (drawApi.Circle == null) return;
        drawApi.Circle(cx, cy, radius, color, thickness, segments);
    }

    internal static void NativeCircleFilled(float cx, float cy, float radius, uint color, int segments)
    {
        if (drawApi.CircleFilled == null) return;
        drawApi.CircleFilled(cx, cy, radius, color, segments);
    }

    internal static void NativeArc(float cx, float cy, float radius, float minAngleRad, float maxAngleRad, uint color, float thickness, int segments)
    {
        if (drawApi.Arc == null) return;
        drawApi.Arc(cx, cy, radius, minAngleRad, maxAngleRad, color, thickness, segments);
    }

    internal static void NativeTriangleFilled(float x0, float y0, float x1, float y1, float x2, float y2, uint color)
    {
        if (drawApi.TriangleFilled == null) return;
        drawApi.TriangleFilled(x0, y0, x1, y1, x2, y2, color);
    }

    internal static void NativeText(string? text, float x, float y, uint color, float fontScale)
    {
        if (drawApi.Text == null) return;

        byte[] bytes = EncodeUtf8(text);
        fixed (byte* pointer = bytes)
        {
            drawApi.Text(pointer, bytes.Length, x, y, color, fontScale);
        }
    }

    internal static vector2 NativeGetTextSize(string? text, float fontScale)
    {
        if (drawApi.GetTextSize == null) return default;

        byte[] bytes = EncodeUtf8(text);
        float width = 0.0f;
        float height = 0.0f;
        fixed (byte* pointer = bytes)
        {
            float* widthPointer = &width;
            float* heightPointer = &height;
            drawApi.GetTextSize(pointer, bytes.Length, fontScale, widthPointer, heightPointer);
        }

        return new vector2(width, height);
    }

    internal static void NativePushClipRect(float minX, float minY, float maxX, float maxY)
    {
        if (drawApi.PushClipRect == null) return;
        drawApi.PushClipRect(minX, minY, maxX, maxY, 0);
    }

    internal static void NativePopClipRect()
    {
        if (drawApi.PopClipRect == null) return;
        drawApi.PopClipRect();
    }
}
