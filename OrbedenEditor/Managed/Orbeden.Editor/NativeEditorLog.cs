using System;
using System.Runtime.InteropServices;
using System.Text;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorLogNativeApi
{
    public delegate* unmanaged[Cdecl]<long*, long*, int> GetRange;
    public delegate* unmanaged[Cdecl]<long, byte*, int, int*, long*, int> CopyEntry;
    public delegate* unmanaged[Cdecl]<int*, void> GetCounts;
    public delegate* unmanaged[Cdecl]<int, byte*, int, void> Append;
    public delegate* unmanaged[Cdecl]<void> Clear;
}
#pragma warning restore CS0649

/// <summary>日志保留窗口的原生访问层。</summary>
internal static unsafe class NativeEditorLog
{
    private static EditorLogNativeApi api;
    private static bool initialized;

    /// <summary>保存日志函数表。</summary>
    internal static void Initialize(EditorLogNativeApi value)
    {
        api = value;
        initialized = api.GetRange != null;
    }

    /// <summary>判断原生日志通道是否可用。</summary>
    internal static bool IsAvailable => initialized && api.Append != null;

    /// <summary>读取保留窗口的序号范围。</summary>
    internal static int GetRange(out long oldestRevision, out long newestRevision)
    {
        oldestRevision = 0;
        newestRevision = 0;
        if (!initialized) return 0;

        long oldest = 0;
        long newest = 0;
        int count = api.GetRange(&oldest, &newest);
        oldestRevision = oldest;
        newestRevision = newest;
        return count;
    }

    /// <summary>按序号取出一条日志，序号已过期时返回 null。</summary>
    internal static string? CopyEntry(long revision, out int level, out long timestampMilliseconds)
    {
        level = 0;
        timestampMilliseconds = 0;
        if (!initialized) return null;

        //先探长度，再取文本，避免为每条日志分配两倍缓冲
        int length = api.CopyEntry(revision, null, 0, null, null);
        if (length < 0) return null;

        byte[] bytes = new byte[length + 1];
        int nativeLevel = 0;
        long nativeTimestamp = 0;
        fixed (byte* pointer = bytes)
            api.CopyEntry(revision, pointer, bytes.Length, &nativeLevel, &nativeTimestamp);

        level = nativeLevel;
        timestampMilliseconds = nativeTimestamp;
        return Encoding.UTF8.GetString(bytes, 0, length);
    }

    /// <summary>统计保留窗口内各级别的条数。</summary>
    internal static void GetCounts(int[] counts)
    {
        if (!initialized || counts.Length < 3) return;
        fixed (int* pointer = counts) api.GetCounts(pointer);
    }

    /// <summary>把托管侧日志写入保留窗口，不重复打印。</summary>
    internal static void Append(int level, string text)
    {
        if (!IsAvailable) return;

        byte[] bytes = Encoding.UTF8.GetBytes(text);
        fixed (byte* pointer = bytes) api.Append(level, pointer, bytes.Length);
    }

    /// <summary>清空保留窗口。</summary>
    internal static void Clear()
    {
        if (initialized && api.Clear != null) api.Clear();
    }
}
