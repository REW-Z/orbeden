using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct RuntimeGuiApi
{
    public delegate* unmanaged<byte*, int, void> Label;
    public delegate* unmanaged<byte*, int, byte> Button;
    public delegate* unmanaged<byte*, int, byte> BeginPanel;
    public delegate* unmanaged<void> EndPanel;
}
#pragma warning restore CS0649

internal static unsafe class NativeGui
{
    private static RuntimeGuiApi api;
    private static bool initialized;

    //保存 C++ 传入的 Runtime GUI 函数表
    internal static void Initialize(IntPtr apiPointer)
    {
        if (apiPointer == IntPtr.Zero)
        {
            initialized = false;
            api = default;
            return;
        }

        api = *(RuntimeGuiApi*)apiPointer;
        initialized = true;
    }

    //绘制文本标签
    internal static void Label(string? text)
    {
        if (!initialized || api.Label == null) return;

        string value = text ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            api.Label(pointer, byteCount);
        }
    }

    //绘制按钮并返回是否点击
    internal static bool Button(string? text)
    {
        if (!initialized || api.Button == null) return false;

        string value = text ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            return api.Button(pointer, byteCount) != 0;
        }
    }

    //开始一个浮动面板
    internal static bool BeginPanel(string? title)
    {
        if (!initialized || api.BeginPanel == null) return false;

        string value = title ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> bytes = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), bytes);

        fixed (byte* pointer = bytes)
        {
            return api.BeginPanel(pointer, byteCount) != 0;
        }
    }

    //结束一个浮动面板
    internal static void EndPanel()
    {
        if (!initialized || api.EndPanel == null) return;
        api.EndPanel();
    }
}
