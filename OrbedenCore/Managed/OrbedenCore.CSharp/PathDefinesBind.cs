using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct PathDefinesBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, int> GetContentRoot;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> GetContentFilePath;
}
#pragma warning restore CS0649

internal static unsafe class PathDefinesBind
{
    private static PathDefinesBindApi api;
    private static bool initialized;

    //保存 C++ 传入的 PathDefines 函数表
    internal static void Initialize(PathDefinesBindApi value)
    {
        api = value;
        initialized = api.GetContentRoot != null;
    }

    //读取当前内容根目录
    internal static string GetContentRoot()
    {
        if (!initialized || api.GetContentRoot == null) return string.Empty;

        int requiredBytes = api.GetContentRoot(null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> bytes = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* pointer = bytes)
        {
            int actualBytes = api.GetContentRoot(pointer, requiredBytes);
            int length = Math.Clamp(actualBytes, 0, requiredBytes);
            return Encoding.UTF8.GetString(bytes[..length]);
        }
    }

    //解析内容相对路径
    internal static string GetContentFilePath(string? path)
    {
        if (!initialized || api.GetContentFilePath == null) return path ?? string.Empty;

        string value = path ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> input = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), input);

        fixed (byte* inputPointer = input)
        {
            int requiredBytes = api.GetContentFilePath(inputPointer, byteCount, null, 0);
            if (requiredBytes <= 0) return string.Empty;

            Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
            fixed (byte* outputPointer = output)
            {
                int actualBytes = api.GetContentFilePath(inputPointer, byteCount, outputPointer, requiredBytes);
                int length = Math.Clamp(actualBytes, 0, requiredBytes);
                return Encoding.UTF8.GetString(output[..length]);
            }
        }
    }
}
