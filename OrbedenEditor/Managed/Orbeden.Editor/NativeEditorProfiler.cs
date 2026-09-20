using System;
using System.Runtime.InteropServices;
using System.Text;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorProfilerNativeApi
{
    public delegate* unmanaged[Cdecl]<byte, void> SetCapturing;
    public delegate* unmanaged[Cdecl]<byte> IsCapturing;
    public delegate* unmanaged[Cdecl]<void> ClearFrames;
    public delegate* unmanaged[Cdecl]<long*, long*, int> GetFrameRange;
    public delegate* unmanaged[Cdecl]<ProfileFrameSummary*, int, int> CopyFrameSummaries;
    public delegate* unmanaged[Cdecl]<long, ProfileEvent*, int, int> CopyFrameEvents;
    public delegate* unmanaged[Cdecl]<int> GetNameCount;
    public delegate* unmanaged[Cdecl]<int, byte*, int, int> CopyName;
}
#pragma warning restore CS0649

/// <summary>一帧内的一次采样调用，字段顺序与原生侧一一对应。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct ProfileEvent
{
    public int NameId;
    public int NodeId;
    public int ParentNodeId;
    public int Category;
    public int Depth;
    public long StartMicroseconds;
    public long DurationMicroseconds;
}

/// <summary>一帧的采样摘要，字段顺序与原生侧一一对应。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct ProfileFrameSummary
{
    //分类耗时按 ProfileCategory 顺序逐个展开，与原生侧的数组字段布局一致
    public long FrameIndex;
    public long DeltaTimeMicroseconds;
    public long OtherMicroseconds;
    public long ScriptMicroseconds;
    public long RenderMicroseconds;
    public long PhysicsMicroseconds;
    public long FileIOMicroseconds;
    public long EditorMicroseconds;
    public long RootMicroseconds;

    //限帧等待：帧率节流主动睡掉的时间
    public long WaitMicroseconds;

    public int EventCount;
    public int DroppedEventCount;

    /// <summary>读取指定分类的耗时。</summary>
    internal readonly long GetCategoryMicroseconds(int category) => category switch
    {
        0 => OtherMicroseconds,
        1 => ScriptMicroseconds,
        2 => RenderMicroseconds,
        3 => PhysicsMicroseconds,
        4 => FileIOMicroseconds,
        5 => EditorMicroseconds,
        _ => 0,
    };
}

/// <summary>性能剖析数据的原生访问层。</summary>
internal static unsafe class NativeEditorProfiler
{
    //采样名在原生侧按 64 字节截断
    private const int NameCapacity = 64;
    private const int CategoryCount = 6;

    private static EditorProfilerNativeApi api;
    private static bool initialized;

    /// <summary>保存剖析函数表。</summary>
    internal static void Initialize(EditorProfilerNativeApi value)
    {
        api = value;
        initialized = api.SetCapturing != null;
    }

    /// <summary>判断原生剖析通道是否可用。</summary>
    internal static bool IsAvailable => initialized && api.CopyFrameSummaries != null;

    /// <summary>开关按帧采集。</summary>
    internal static void SetCapturing(bool value)
    {
        if (initialized && api.SetCapturing != null) api.SetCapturing(value ? (byte)1 : (byte)0);
    }

    /// <summary>判断是否正在按帧采集。</summary>
    internal static bool IsCapturing()
    {
        return initialized && api.IsCapturing != null && api.IsCapturing() != 0;
    }

    /// <summary>清空帧历史，不动采样树。</summary>
    internal static void ClearFrames()
    {
        if (initialized && api.ClearFrames != null) api.ClearFrames();
    }

    /// <summary>读取帧历史的帧序号范围。</summary>
    internal static int GetFrameRange(out long oldestFrame, out long newestFrame)
    {
        oldestFrame = 0;
        newestFrame = -1;
        if (!initialized) return 0;

        long oldest = 0;
        long newest = 0;
        int count = api.GetFrameRange(&oldest, &newest);
        oldestFrame = oldest;
        newestFrame = newest;
        return count;
    }

    /// <summary>读取保留窗口内的帧摘要，按帧序号升序。</summary>
    internal static int CopyFrameSummaries(ProfileFrameSummary[] frames)
    {
        if (!IsAvailable || frames.Length == 0) return 0;
        fixed (ProfileFrameSummary* pointer = frames) return api.CopyFrameSummaries(pointer, frames.Length);
    }

    /// <summary>读取指定帧的采样事件。</summary>
    internal static int CopyFrameEvents(long frameIndex, ProfileEvent[] events)
    {
        if (!IsAvailable || api.CopyFrameEvents == null || events.Length == 0) return 0;
        fixed (ProfileEvent* pointer = events) return api.CopyFrameEvents(frameIndex, pointer, events.Length);
    }

    /// <summary>读取驻留表里的采样名，编号无效时返回空串。</summary>
    internal static string CopyName(int nameId)
    {
        if (!IsAvailable || api.CopyName == null || nameId < 0) return string.Empty;

        byte[] bytes = new byte[NameCapacity];
        int length = 0;
        fixed (byte* pointer = bytes) length = api.CopyName(nameId, pointer, bytes.Length);
        return length <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, Math.Min(length, NameCapacity - 1));
    }

    /// <summary>读取分类数量。</summary>
    internal static int GetCategoryCount() => CategoryCount;
}
