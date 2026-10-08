using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>
/// 一个原生 UI 上下文的 C# 薄层。只做三件事：把 Span 展开成指针加数量、检查负返回值并转成异常、
/// 把托管数据复制到原生缓冲。不含任何布局、批次或控件决策。
/// </summary>
public sealed unsafe class RetainedGuiBridge : IRetainedGuiHost, IUIDerivedPositionSink
{
    private readonly ulong context;

    internal RetainedGuiBridge(ulong context)
    {
        this.context = context;
    }

    /// <summary>原生上下文句柄；为零表示没有可用上下文。</summary>
    public ulong Context => context;

    /// <summary>上下文是否可用。</summary>
    public bool IsValid => context != 0 && RetainedGuiNative.IsAvailable;

    /// <summary>创建原生上下文；原生侧未接入时返回空。</summary>
    internal static RetainedGuiBridge? Create(ulong worldRevision, ulong managedGeneration)
    {
        if (!RetainedGuiNative.IsAvailable) return null;
        ulong handle = RetainedGuiNative.CreateContext(worldRevision, managedGeneration);
        //诊断：上下文生命周期必须成对，句柄被销毁后仍被调用就是这里能看出来的征兆。
        Console.Error.WriteLine($"RetainedGuiBridge: create 0x{handle:X} (revision {worldRevision}, generation {managedGeneration}).");
        return handle == 0 ? null : new RetainedGuiBridge(handle);
    }

    /// <summary>销毁原生上下文；重复调用无副作用。</summary>
    public void Destroy()
    {
        Console.Error.WriteLine($"RetainedGuiBridge: destroy 0x{context:X}.");
        RetainedGuiNative.DestroyContext(context);
    }

    /// <summary>提交网格变更；顶点与索引放在同一段上传缓冲里。</summary>
    public bool UpdateMeshes(ReadOnlySpan<UIMeshUpdate> updates, ReadOnlySpan<UIVertex> vertices, ReadOnlySpan<uint> indices)
    {
        if (!IsValid) return false;
        fixed (UIMeshUpdate* updatePointer = updates)
        fixed (UIVertex* vertexPointer = vertices)
        fixed (uint* indexPointer = indices)
        {
            return RetainedGuiNative.UpdateMeshes(context, updatePointer, updates.Length,
                vertexPointer, vertices.Length, indexPointer, indices.Length) != 0;
        }
    }

    /// <summary>释放不再被引用的网格。</summary>
    public bool RemoveMeshes(ReadOnlySpan<ulong> meshIds)
    {
        if (!IsValid) return false;
        fixed (ulong* pointer = meshIds)
            return RetainedGuiNative.RemoveMeshes(context, pointer, meshIds.Length) != 0;
    }

    /// <summary>提交一块画布的命令与矩阵。</summary>
    public bool SubmitCanvas(in UICanvasSubmission canvas, ReadOnlySpan<UIDrawCommand> commands, ReadOnlySpan<matrix4x4> matrices)
    {
        if (!IsValid) return false;
        UICanvasSubmission copy = canvas;
        fixed (UIDrawCommand* commandPointer = commands)
        fixed (matrix4x4* matrixPointer = matrices)
        {
            return RetainedGuiNative.SubmitCanvas(context, &copy, commandPointer, commands.Length,
                matrixPointer, matrices.Length) != 0;
        }
    }

    /// <summary>原子发布本帧。</summary>
    public bool EndFrame(ulong frameId) => IsValid && RetainedGuiNative.EndFrame(context, frameId) != 0;

    /// <summary>批量写入派生位置；条目数不得超过原生侧一次调用的上限时由调用方分批。</summary>
    public bool ApplyDerivedPositions(ReadOnlySpan<UIDerivedPosition> positions)
    {
        if (!IsValid) return false;
        fixed (UIDerivedPosition* pointer = positions)
            return RetainedGuiNative.ApplyDerivedPositions(context, pointer, positions.Length) != 0;
    }

    /// <summary>读取视图快照。容量不足时按原生约定不部分写，返回所需条数。</summary>
    public Span<UIView> ReadViews(Span<UIView> output)
    {
        if (!IsValid) return [];
        fixed (UIView* pointer = output)
        {
            int required = RetainedGuiNative.ReadViews(context, pointer, output.Length);
            if (required < 0)
                Console.Error.WriteLine($"RetainedGuiBridge: ReadViews failed on 0x{context:X} (capacity {output.Length}).");
            ThrowIfFailed(required, "ReadViews");
            return required <= output.Length ? output[..required] : [];
        }
    }

    /// <summary>查询视图条数；容量不足时原生侧不部分写，这里只取数量。</summary>
    public int CountViews() => IsValid ? Math.Max(0, RetainedGuiNative.ReadViews(context, null, 0)) : 0;

    /// <summary>主显示目标的像素尺寸；渲染器还没报过时为假。</summary>
    public bool TryReadDisplaySize(out int width, out int height)
    {
        width = 0;
        height = 0;
        return IsValid && RetainedGuiNative.TryReadDisplaySize(out width, out height);
    }

    /// <summary>读取世界结构变化。复制成功才确认排空；fullResync 表示必须重建整个索引。</summary>
    /// <summary>窗口里的待消费输入条数。</summary>
    public int CountInput() => IsValid ? Math.Max(0, RetainedGuiNative.ReadInput(context, null, 0, null, 0, null)) : 0;

    /// <summary>
    /// 读取待消费输入。容量不足时原生侧不部分写也不消费，
    /// 返回所需条数并把 textBytes 写成所需的文本字节数。
    /// </summary>
    public int ReadInput(Span<UIInputRecord> output, Span<byte> text, out int textBytes)
    {
        textBytes = 0;
        if (!IsValid) return 0;
        fixed (UIInputRecord* records = output)
        fixed (byte* textPointer = text)
        {
            int required = 0;
            fixed (int* bytes = &textBytes)
            {
                required = RetainedGuiNative.ReadInput(context, records, output.Length, textPointer, text.Length, bytes);
            }
            return Math.Max(0, required);
        }
    }

    /// <summary>确认消费：被消费的按下会占有对应键，抬起或失焦时放手。</summary>
    public void ConsumeInput(ReadOnlySpan<ulong> sequences)
    {
        if (!IsValid || sequences.Length == 0) return;
        fixed (ulong* pointer = sequences) RetainedGuiNative.ConsumeInput(context, pointer, sequences.Length);
    }

    public bool ReadChanges(Span<UISceneChange> output, out int required, out bool fullResync)
    {
        required = 0;
        fullResync = false;
        if (!IsValid) return false;

        byte resync = 0;
        fixed (UISceneChange* pointer = output)
        {
            required = RetainedGuiNative.ReadChanges(context, pointer, output.Length, &resync);
        }
        ThrowIfFailed(required, "ReadChanges");
        fullResync = resync != 0;
        return required <= output.Length;
    }

    /// <summary>读取剪贴板文本；容量不足时返回所需字节数，不做部分写。</summary>
    public int ReadClipboard(Span<byte> output)
    {
        if (!IsValid) return 0;
        fixed (byte* pointer = output)
        {
            int required = RetainedGuiNative.ReadClipboard(pointer, output.Length);
            ThrowIfFailed(required, "ReadClipboard");
            return required;
        }
    }

    /// <summary>写入剪贴板。</summary>
    public bool WriteClipboard(ReadOnlySpan<byte> text)
    {
        if (!IsValid) return false;
        fixed (byte* pointer = text)
            return RetainedGuiNative.WriteClipboard(pointer, text.Length) != 0;
    }

    /// <summary>设置文本输入焦点与光标矩形。</summary>
    public void SetTextInput(ulong token, bool active, int x, int y, int width, int height)
    {
        if (IsValid) RetainedGuiNative.SetTextInput(context, token, active, x, y, width, height);
    }

    /// <summary>读取已呈现帧的窗口深度；不可用或失败时返回假。</summary>
    public bool ReadDepth(ulong viewId, ulong viewerId, ulong presentedFrame, int x, int y, out float depth)
    {
        depth = 0.0f;
        if (!IsValid) return false;
        float value = 0.0f;
        if (RetainedGuiNative.ReadDepth(context, viewId, viewerId, presentedFrame, x, y, &value) == 0) return false;
        depth = value;
        return true;
    }

    /// <summary>查询字形度量。</summary>
    public int QueryGlyphs(ReadOnlySpan<UIGlyphRequest> requests, Span<UIGlyphResult> output)
    {
        if (!IsValid) return 0;
        fixed (UIGlyphRequest* requestPointer = requests)
        fixed (UIGlyphResult* resultPointer = output)
        {
            int required = RetainedGuiNative.QueryGlyphs(context, requestPointer, requests.Length, resultPointer, output.Length);
            ThrowIfFailed(required, "QueryGlyphs");
            return required;
        }
    }

    /// <summary>光栅化字形；返回所需字节数，容量不足时不发布像素。</summary>
    public int RasterizeGlyphs(ReadOnlySpan<UIGlyphRequest> requests, Span<UIGlyphResult> results, Span<byte> pixels)
    {
        if (!IsValid) return 0;
        fixed (UIGlyphRequest* requestPointer = requests)
        fixed (UIGlyphResult* resultPointer = results)
        fixed (byte* pixelPointer = pixels)
        {
            int required = RetainedGuiNative.RasterizeGlyphs(context, requestPointer, requests.Length,
                resultPointer, results.Length, pixelPointer, pixels.Length);
            ThrowIfFailed(required, "RasterizeGlyphs");
            return required;
        }
    }

    /// <summary>读取两个字形的字距。</summary>
    public float GetKerning(int fontObjectId, uint leftGlyph, uint rightGlyph) =>
        RetainedGuiNative.GetKerning(fontObjectId, leftGlyph, rightGlyph);

    //负返回值代表错误，薄层把它转成带操作名的异常；其余值原样返回。
    private static void ThrowIfFailed(int result, string operation)
    {
        if (result < 0) throw new InvalidOperationException($"RetainedGUI native call failed: {operation}.");
    }
}
