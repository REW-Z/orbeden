using System;
using System.Runtime.InteropServices;
using System.Text;

namespace OrbedenCore.CSharp;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct PathDefinesBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, int> GetProjectRoot;
    public delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> GetProjectFilePath;
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
        initialized = api.GetProjectRoot != null;
    }

    //读取当前项目根目录
    internal static string GetProjectRoot()
    {
        if (!initialized || api.GetProjectRoot == null) return string.Empty;

        int requiredBytes = api.GetProjectRoot(null, 0);
        if (requiredBytes <= 0) return string.Empty;

        Span<byte> bytes = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
        fixed (byte* pointer = bytes)
        {
            int actualBytes = api.GetProjectRoot(pointer, requiredBytes);
            int length = Math.Clamp(actualBytes, 0, requiredBytes);
            return Encoding.UTF8.GetString(bytes[..length]);
        }
    }

    //解析项目相对路径
    internal static string GetProjectFilePath(string? path)
    {
        if (!initialized || api.GetProjectFilePath == null) return path ?? string.Empty;

        string value = path ?? string.Empty;
        int byteCount = Encoding.UTF8.GetByteCount(value);
        Span<byte> input = byteCount <= 1024 ? stackalloc byte[Math.Max(byteCount, 1)] : new byte[byteCount];
        Encoding.UTF8.GetBytes(value.AsSpan(), input);

        fixed (byte* inputPointer = input)
        {
            int requiredBytes = api.GetProjectFilePath(inputPointer, byteCount, null, 0);
            if (requiredBytes <= 0) return string.Empty;

            Span<byte> output = requiredBytes <= 1024 ? stackalloc byte[requiredBytes] : new byte[requiredBytes];
            fixed (byte* outputPointer = output)
            {
                int actualBytes = api.GetProjectFilePath(inputPointer, byteCount, outputPointer, requiredBytes);
                int length = Math.Clamp(actualBytes, 0, requiredBytes);
                return Encoding.UTF8.GetString(output[..length]);
            }
        }
    }
}
