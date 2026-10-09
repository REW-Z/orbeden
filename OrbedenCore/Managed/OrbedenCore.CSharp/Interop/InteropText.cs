using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Orbeden;

/// <summary>统一处理托管文本、原生 UTF-8 和文件编码配置。</summary>
public static class InteropText
{
    /// <summary>取得框架默认的 UTF-8 编码配置。</summary>
    public static Encoding Utf8 => Encoding.UTF8;

    /// <summary>创建指定 BOM 和非法字符处理规则的 UTF-8 编码配置。</summary>
    public static UTF8Encoding CreateUtf8Encoding(bool emitBom, bool throwOnInvalidBytes = false) =>
        new(emitBom, throwOnInvalidBytes);

    /// <summary>计算文本转换后的 UTF-8 字节数。</summary>
    public static int GetUtf8ByteCount(string text) => Encoding.UTF8.GetByteCount(text);

    /// <summary>计算字符区域转换后的 UTF-8 字节数。</summary>
    public static int GetUtf8ByteCount(ReadOnlySpan<char> text) => Encoding.UTF8.GetByteCount(text);

    /// <summary>将文本编码为独立的 UTF-8 字节数组。</summary>
    public static byte[] EncodeUtf8(string text) => Encoding.UTF8.GetBytes(text);

    /// <summary>将字符区域编码到调用方提供的缓冲。</summary>
    public static int EncodeUtf8(ReadOnlySpan<char> text, Span<byte> output) => Encoding.UTF8.GetBytes(text, output);

    /// <summary>解码 UTF-8 字节区域。</summary>
    public static string DecodeUtf8(ReadOnlySpan<byte> bytes) => Encoding.UTF8.GetString(bytes);

    /// <summary>解码完整的 UTF-8 字节数组，并保留空引用校验。</summary>
    public static string DecodeUtf8(byte[] bytes) => Encoding.UTF8.GetString(bytes);

    /// <summary>解码数组中的指定 UTF-8 字节范围。</summary>
    public static string DecodeUtf8(byte[] bytes, int offset, int count) => Encoding.UTF8.GetString(bytes, offset, count);

    /// <summary>解码调用期间有效的原生 UTF-8 字节区域。</summary>
    public static unsafe string DecodeUtf8(byte* bytes, int count) => Encoding.UTF8.GetString(bytes, count);

    /// <summary>读取以零结束的原生 UTF-8 文本；空指针返回空引用。</summary>
    public static string? ReadUtf8Terminated(IntPtr text) => Marshal.PtrToStringUTF8(text);

    /// <summary>读取缓冲中以零结束的 UTF-8 文本。</summary>
    public static string ReadUtf8Terminated(ReadOnlySpan<byte> bytes)
    {
        int end = bytes.IndexOf((byte)0);
        return DecodeUtf8(end < 0 ? bytes : bytes[..end]);
    }

    /// <summary>写入以零结束的 UTF-8 文本，在完整字符边界截断到缓冲容量。</summary>
    public static int WriteUtf8Terminated(ReadOnlySpan<char> text, Span<byte> output)
    {
        if (output.IsEmpty) return 0;
        Encoding.UTF8.GetEncoder().Convert(text, output[..^1], true, out _, out int written, out _);
        output[written] = 0;
        return written;
    }
}
