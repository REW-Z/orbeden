using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct FileSystemBindApi
{
    public delegate* unmanaged[Cdecl]<byte*, int, NativeBindingBuffer*, byte> ReadRawContent;
}
#pragma warning restore CS0649

/// <summary>读取未经导入和 Cook 的原始内容。</summary>
public static unsafe class FileSystem
{
    private static FileSystemBindApi api;

    //保存原生文件系统函数表
    internal static void InitializeNativeApi(FileSystemBindApi value) => api = value;

    /// <summary>按 RawContent 相对路径读取原始字节，失败返回假和空数组；空文件读取成功。</summary>
    public static bool ReadRawContent(string path, out byte[] content)
    {
        ArgumentNullException.ThrowIfNull(path);
        content = [];
        if (api.ReadRawContent == null) return false;
        byte[] pathBytes = InteropText.EncodeUtf8(path);
        NativeBindingBuffer buffer = default;
        fixed (byte* pointer = pathBytes)
            if (api.ReadRawContent(pointer, pathBytes.Length, &buffer) == 0) return false;
        try
        {
            content = buffer.Span.ToArray();
            return true;
        }
        finally
        {
            NativeBindingRuntime.Release(buffer);
        }
    }
}
