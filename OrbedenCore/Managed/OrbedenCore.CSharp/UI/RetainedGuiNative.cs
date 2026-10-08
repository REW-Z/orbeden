using System;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>
/// RetainedGUI 原生函数表的托管镜像。表头之后是固定顺序的槽位，与原生
/// RetainedGuiBridge.h 的 RetainedGuiApi 一一对应；任何一侧改字段顺序都会在构建期被尺寸断言拦下。
/// 底层 ABI 把每个 Span 展开为指针加 int32 数量，bool 用 byte，全部为 Cdecl。
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct RetainedGuiApi
{
    /// <summary>表版本。当前为 2。</summary>
    public uint version;

    /// <summary>表字节数。</summary>
    public uint structSize;

    public delegate* unmanaged[Cdecl]<ulong, ulong, ulong> CreateContext;
    public delegate* unmanaged[Cdecl]<ulong, void> DestroyContext;
    public delegate* unmanaged[Cdecl]<ulong, UIMeshUpdate*, int, UIVertex*, int, uint*, int, byte> UpdateMeshes;
    public delegate* unmanaged[Cdecl]<ulong, ulong*, int, byte> RemoveMeshes;
    public delegate* unmanaged[Cdecl]<ulong, UICanvasSubmission*, UIDrawCommand*, int, matrix4x4*, int, byte> SubmitCanvas;
    public delegate* unmanaged[Cdecl]<ulong, ulong, byte> EndFrame;
    public delegate* unmanaged[Cdecl]<ulong, UIView*, int, int> ReadViews;
    public delegate* unmanaged[Cdecl]<ulong, UIInputRecord*, int, byte*, int, int*, int> ReadInput;
    public delegate* unmanaged[Cdecl]<ulong, ulong*, int, void> ConsumeInput;
    public delegate* unmanaged[Cdecl]<ulong, UIDerivedPosition*, int, byte> ApplyDerivedPositions;
    public delegate* unmanaged[Cdecl]<ulong, ulong, ulong, ulong, int, int, float*, byte> ReadDepth;
    public delegate* unmanaged[Cdecl]<ulong, UIGlyphRequest*, int, UIGlyphResult*, int, int> QueryGlyphs;
    public delegate* unmanaged[Cdecl]<ulong, UIGlyphRequest*, int, UIGlyphResult*, int, byte*, int, int> RasterizeGlyphs;
    public delegate* unmanaged[Cdecl]<int, uint, uint, float> GetKerning;
    public delegate* unmanaged[Cdecl]<ulong, ulong, byte, int, int, int, int, void> SetTextInput;
    public delegate* unmanaged[Cdecl]<byte*, int, int> ReadClipboard;
    public delegate* unmanaged[Cdecl]<byte*, int, byte> WriteClipboard;
    public delegate* unmanaged[Cdecl]<ulong, UISceneChange*, int, byte*, int> ReadChanges;

    /// <summary>读取主显示目标的像素尺寸；屏幕画布的首帧视口引导用它。</summary>
    public delegate* unmanaged[Cdecl]<int*, int*, byte> ReadDisplaySize;
}

/// <summary>RetainedGUI 函数表的进程级持有者。表在进程内稳定，不随世界切换变化。</summary>
internal static unsafe class RetainedGuiNative
{
    /// <summary>当前表版本；与原生 RetainedGuiApi::version 必须一致。</summary>
    internal const uint ExpectedVersion = 2;

    private static RetainedGuiApi api;
    private static bool initialized;

    /// <summary>函数表是否已经接入。为空表示原生侧没有提供或版本不符。</summary>
    internal static bool IsAvailable => initialized;

    /// <summary>接入原生函数表；版本或尺寸不符时拒绝并返回假。</summary>
    internal static bool Initialize(IntPtr table)
    {
        initialized = false;
        api = default;
        //表还没绑定不算错误：模块初始化可能早于引擎 API 绑定，由世界附着时兜底重试。
        if (table == IntPtr.Zero) return false;

        RetainedGuiApi candidate = *(RetainedGuiApi*)table;
        if (candidate.version != ExpectedVersion
            || candidate.structSize != sizeof(RetainedGuiApi)
            || candidate.CreateContext == null
            || candidate.DestroyContext == null)
        {
            Console.Error.WriteLine(
                $"RetainedGuiNative: 函数表不匹配（表 0x{table.ToInt64():X}，版本 {candidate.version}，尺寸 {candidate.structSize}，期望 {ExpectedVersion}/{sizeof(RetainedGuiApi)}）。");
            return false;
        }

        api = candidate;
        initialized = true;
        return true;
    }

    /// <summary>世界附着时兜底接入：模块初始化可能早于引擎 API 绑定，那时表还是空的。</summary>
    internal static bool EnsureInitialized()
    {
        if (initialized) return true;
        IntPtr table = OrbedenCoreRuntime.RetainedGuiApi;
        if (table != IntPtr.Zero) return Initialize(table);

        if (!reportedMissingTable)
        {
            reportedMissingTable = true;
            Console.Error.WriteLine("RetainedGuiNative: 原生函数表尚未绑定，UI 只做布局与数据，不向原生提交。");
        }
        return false;
    }

    //未接入的提示只报一次，避免世界切换时刷屏。
    private static bool reportedMissingTable;

    /// <summary>断开函数表；程序集卸载时调用。</summary>
    internal static void Shutdown()
    {
        api = default;
        initialized = false;
    }

    internal static ulong CreateContext(ulong worldRevision, ulong managedGeneration) =>
        initialized ? api.CreateContext(worldRevision, managedGeneration) : 0;

    internal static void DestroyContext(ulong context)
    {
        if (initialized && context != 0) api.DestroyContext(context);
    }

    internal static byte UpdateMeshes(ulong context, UIMeshUpdate* updates, int updateCount,
        UIVertex* vertices, int vertexCount, uint* indices, int indexCount) =>
        initialized ? api.UpdateMeshes(context, updates, updateCount, vertices, vertexCount, indices, indexCount) : (byte)0;

    internal static byte RemoveMeshes(ulong context, ulong* meshIds, int count) =>
        initialized ? api.RemoveMeshes(context, meshIds, count) : (byte)0;

    internal static byte SubmitCanvas(ulong context, UICanvasSubmission* canvas,
        UIDrawCommand* commands, int commandCount, matrix4x4* matrices, int matrixCount) =>
        initialized ? api.SubmitCanvas(context, canvas, commands, commandCount, matrices, matrixCount) : (byte)0;

    internal static byte EndFrame(ulong context, ulong frameId) =>
        initialized ? api.EndFrame(context, frameId) : (byte)0;

    internal static int ReadViews(ulong context, UIView* output, int capacity) =>
        initialized ? api.ReadViews(context, output, capacity) : 0;

    internal static int ReadInput(ulong context, UIInputRecord* output, int capacity,
        byte* text, int textCapacity, int* textBytes)
    {
        if (!initialized || textBytes == null) return 0;
        return api.ReadInput(context, output, capacity, text, textCapacity, textBytes);
    }

    internal static void ConsumeInput(ulong context, ulong* sequences, int count)
    {
        if (initialized && count > 0) api.ConsumeInput(context, sequences, count);
    }

    internal static byte ApplyDerivedPositions(ulong context, UIDerivedPosition* positions, int count) =>
        initialized ? api.ApplyDerivedPositions(context, positions, count) : (byte)0;

    internal static byte ReadDepth(ulong context, ulong viewId, ulong viewerId, ulong presentedFrame,
        int x, int y, float* depth) =>
        initialized ? api.ReadDepth(context, viewId, viewerId, presentedFrame, x, y, depth) : (byte)0;

    internal static int QueryGlyphs(ulong context, UIGlyphRequest* requests, int requestCount, UIGlyphResult* output, int capacity) =>
        initialized ? api.QueryGlyphs(context, requests, requestCount, output, capacity) : 0;

    internal static int RasterizeGlyphs(ulong context, UIGlyphRequest* requests, int requestCount,
        UIGlyphResult* output, int resultCapacity, byte* pixels, int pixelCapacity) =>
        initialized ? api.RasterizeGlyphs(context, requests, requestCount, output, resultCapacity, pixels, pixelCapacity) : 0;

    internal static float GetKerning(int fontObjectId, uint leftGlyph, uint rightGlyph) =>
        initialized ? api.GetKerning(fontObjectId, leftGlyph, rightGlyph) : 0.0f;

    internal static void SetTextInput(ulong context, ulong token, bool active, int x, int y, int width, int height)
    {
        if (initialized) api.SetTextInput(context, token, active ? (byte)1 : (byte)0, x, y, width, height);
    }

    internal static int ReadClipboard(byte* output, int capacity) =>
        initialized ? api.ReadClipboard(output, capacity) : 0;

    internal static byte WriteClipboard(byte* text, int length) =>
        initialized ? api.WriteClipboard(text, length) : (byte)0;

    internal static int ReadChanges(ulong context, UISceneChange* output, int capacity, byte* fullResync)
    {
        if (!initialized || fullResync == null) return 0;
        return api.ReadChanges(context, output, capacity, fullResync);
    }

    internal static bool TryReadDisplaySize(out int width, out int height)
    {
        width = 0;
        height = 0;
        if (!initialized || api.ReadDisplaySize == null) return false;

        int reportedWidth = 0;
        int reportedHeight = 0;
        if (api.ReadDisplaySize(&reportedWidth, &reportedHeight) == 0) return false;
        width = reportedWidth;
        height = reportedHeight;
        return width > 0 && height > 0;
    }
}
