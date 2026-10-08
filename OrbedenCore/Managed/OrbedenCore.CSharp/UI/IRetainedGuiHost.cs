using System;

namespace Orbeden;

/// <summary>
/// 帧数据到原生渲染器的传输口。四个方法与 RetainedGuiApi 的网格与画布槽一一对应，
/// 由原生 UI 上下文实现；帧构建器只负责产出数据，不关心传输细节。
/// </summary>
public interface IRetainedGuiHost
{
    /// <summary>提交本帧的网格变更；顶点与索引放在同一段上传缓冲里。</summary>
    /// <returns>全部更新都被接受时返回真。</returns>
    bool UpdateMeshes(ReadOnlySpan<UIMeshUpdate> updates, ReadOnlySpan<UIVertex> vertices, ReadOnlySpan<uint> indices);

    /// <summary>释放不再被引用的网格。</summary>
    bool RemoveMeshes(ReadOnlySpan<ulong> meshIds);

    /// <summary>提交一块画布的命令与矩阵；长度、索引与裁剪栈由原生侧复核。</summary>
    bool SubmitCanvas(in UICanvasSubmission canvas, ReadOnlySpan<UIDrawCommand> commands, ReadOnlySpan<matrix4x4> matrices);

    /// <summary>原子发布本帧；失败时该上下文本帧不绘制。</summary>
    bool EndFrame(ulong frameId);
}
