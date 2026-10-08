using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>
/// 帧构建：把画布子树变成网格更新、绘制命令与命中记录。原生侧只执行命令，
/// 不遍历控件树、不重新排列布局、也不重新决定批次。
/// 命令按画布组织，每块画布一条提交；命中列表与绘制遍历同源，成功呈现后才切换。
/// </summary>
public sealed class UIFrameBuilder
{
    private sealed class CanvasBatch
    {
        internal UICanvasSubmission Submission;
        //这块画布子树的根；空提交时为节点树里的同一个节点，不额外遍历。
        internal UINode? Root;
        internal readonly List<UIDrawCommand> Commands = [];
        internal readonly List<matrix4x4> Matrices = [];
    }

    //裁剪形状与控件附加网格各自从不同档位编片段号，不会与图形的片段撞号。
    private const int ClipFragmentBase = 1 << 16;
    private const int OverlayFragmentBase = 2 << 16;

    private readonly UIWorldContext context;
    private readonly UIGeometryCache geometry = new();
    private readonly UIClipStack clips = new();
    private readonly UIMeshBuilder clipShape = new();
    private readonly List<UIMeshUpdate> meshUpdates = [];
    private readonly List<UIVertex> uploadVertices = [];
    private readonly List<uint> uploadIndices = [];
    private readonly List<ulong> removedMeshes = [];
    private readonly List<CanvasBatch> batches = [];
    private readonly List<UINode?> traversal = [];
    private readonly List<UIHitRecord> pendingHits = [];
    private readonly List<UIHitRecord> presentedHits = [];

    private int batchCount;
    private ulong frameId;
    private bool frameActive;

    internal UIFrameBuilder(UIWorldContext context)
    {
        this.context = context;
    }

    /// <summary>帧数据的目标；为空时只产出命中列表，不提交。</summary>
    public IRetainedGuiHost? Host { get; set; }

    /// <summary>裁剪栈；命中检测与帧命令共用同一份层数据。</summary>
    public UIClipStack ClipStack => clips;

    /// <summary>最近一次成功呈现的命中列表，按绘制顺序排列。</summary>
    public IReadOnlyList<UIHitRecord> PresentedHits => presentedHits;

    /// <summary>最近一次成功呈现的帧号；首次呈现前为零。</summary>
    public ulong PresentedFrame { get; private set; }

    /// <summary>开始一帧：清空上一帧的内容但保留容量。</summary>
    public void BeginFrame(ulong id)
    {
        frameId = id;
        frameActive = true;
        meshUpdates.Clear();
        uploadVertices.Clear();
        uploadIndices.Clear();
        removedMeshes.Clear();
        pendingHits.Clear();
        batchCount = 0;
    }

    /// <summary>构建一块画布的命令与矩阵。画布内的遍历顺序即绘制顺序。</summary>
    public void BuildCanvas(Canvas canvas, in UIView view)
    {
        if (!frameActive) return;
        CanvasBatch? batch = BuildSubmission(canvas, view);
        if (batch == null) return;
        UINode root = batch.Root!;
        //先普通内容、再弹层：两层各自做一次前序遍历，父图形先于子图形。
        for (int layer = 0; layer <= 1; ++layer)
            Traverse(root, layer, canvas, view.viewId, batch);
    }

    /// <summary>提交一块没有命令的画布；离屏输出靠它每帧清成透明。</summary>
    public void BuildEmptyCanvas(Canvas canvas, in UIView view)
    {
        if (!frameActive) return;
        BuildSubmission(canvas, view);
    }

    //一块画布的提交头：目标、矩阵与清屏信息。返回空表示这块画布这一帧不该提交。
    private CanvasBatch? BuildSubmission(Canvas canvas, in UIView view)
    {
        UINode? root = context.FindNode(canvas.EnsId);
        if (root == null) return null;

        //画布本地到裁剪空间的矩阵由托管侧算好：布局与逻辑尺寸都在这一侧。
        //世界空间画布提交的是本地到世界的变换，原生侧再乘相机投影。
        vector2 logicalSize = view.logicalSize;
        matrix4x4 viewProjection = ComposeCanvasProjection(canvas, root, logicalSize, context.IsEditorMode);
        //离屏画布把输出纹理交给原生侧，绘制与清屏都由它执行。
        Texture2D? output = canvas.GetOutputTexture();
        if (canvas.GetRenderMode() == CanvasRenderMode.Offscreen && output == null) return null;

        CanvasBatch batch = AcquireBatch();
        batch.Root = root;
        batch.Submission = new UICanvasSubmission
        {
            frameId = frameId,
            canvasId = unchecked((ulong)(uint)canvas.InstanceId),
            viewId = view.viewId,
            outputTextureObjectId = output?.InstanceId ?? 0,
            renderMode = (uint)canvas.GetRenderMode(),
            sortOrder = canvas.GetSortOrder(),
            drawLayer = unchecked((uint)canvas.GetDrawLayer()),
            width = (int)logicalSize.x,
            height = (int)logicalSize.y,
            viewProjection = viewProjection,
        };
        return batch;
    }

    /// <summary>提交本帧。任一步失败都不发布命中列表，该帧不显示半帧。</summary>
    public bool Submit()
    {
        if (!frameActive) return false;
        frameActive = false;

        IRetainedGuiHost? host = Host;
        if (host == null)
        {
            pendingHits.Clear();
            return false;
        }

        geometry.CollectUnused(frameId, removedMeshes);

        bool accepted = meshUpdates.Count == 0
            || host.UpdateMeshes(CollectionsMarshal.AsSpan(meshUpdates),
                CollectionsMarshal.AsSpan(uploadVertices), CollectionsMarshal.AsSpan(uploadIndices));
        if (removedMeshes.Count != 0) accepted &= host.RemoveMeshes(CollectionsMarshal.AsSpan(removedMeshes));
        for (int index = 0; index < batchCount; ++index)
        {
            CanvasBatch batch = batches[index];
            accepted &= host.SubmitCanvas(batch.Submission,
                CollectionsMarshal.AsSpan(batch.Commands), CollectionsMarshal.AsSpan(batch.Matrices));
        }
        accepted &= host.EndFrame(frameId);

        if (!accepted)
        {
            pendingHits.Clear();
            return false;
        }

        //呈现成功才切换命中快照：输入阶段用的是上一帧已经画出来的东西。
        presentedHits.Clear();
        presentedHits.AddRange(pendingHits);
        PresentedFrame = frameId;
        return true;
    }

    /// <summary>上下文销毁：清空缓存与快照。</summary>
    internal void Reset(ulong contextGeneration)
    {
        geometry.Reset(contextGeneration);
        clips.Clear();
        presentedHits.Clear();
        pendingHits.Clear();
        PresentedFrame = 0;
        frameActive = false;
        batchCount = 0;
    }

    //画布本地坐标到裁剪空间的矩阵。屏幕与离屏画布是逻辑像素到正交裁剪空间；
    //世界空间画布提交本地到世界的变换，由原生侧再乘相机视图投影。
    //顶点一直待在画布本地逻辑单位里：世界空间的缩放来自画布节点的 Transform，
    //这里再叠一次正交会把尺寸缩到与配置无关的两倍放大倍数。
    private static matrix4x4 ComposeCanvasProjection(Canvas canvas, UINode root, vector2 logicalSize, bool editorMode)
    {
        if (canvas.GetRenderMode() == CanvasRenderMode.WorldSpace) return root.WorldMatrix;
        if (editorMode && canvas.GetRenderMode() == CanvasRenderMode.Overlay) return root.ScenePreviewMatrix;
        return UIMatrix.Ortho(0.0f, logicalSize.x, 0.0f, logicalSize.y);
    }

    //取得一块可复用的画布批次，超出已有容量时新建。
    private CanvasBatch AcquireBatch()
    {
        if (batchCount < batches.Count)
        {
            CanvasBatch reused = batches[batchCount++];
            reused.Root = null;
            reused.Commands.Clear();
            reused.Matrices.Clear();
            return reused;
        }
        CanvasBatch created = new();
        batches.Add(created);
        ++batchCount;
        return created;
    }

    //按层次前序遍历一棵子树；同一节点上的图形先于它的子节点。
    //带裁剪的节点在子节点之后补一个空标记，出栈时弹掉自己那一层。
    private void Traverse(UINode root, int layer, Canvas canvas, ulong viewId, CanvasBatch batch)
    {
        traversal.Clear();
        traversal.Add(root);
        while (traversal.Count != 0)
        {
            UINode? node = traversal[^1];
            traversal.RemoveAt(traversal.Count - 1);
            if (node == null)
            {
                PopClip(batch);
                continue;
            }

            //滚动内容：裁剪只包围 content 子树，容器自己的滚动条不受影响。
            if (TryResolveScrollContent(node, out UILayout? viewport))
            {
                clips.PushRectangle(viewport);
                PushClip(batch);
                traversal.Add(null);
            }

            //裁剪包住同节点图形与整棵子树，所以先入栈再输出本节点。
            Mask? mask = node.GetElement<Mask>();
            if (mask != null)
            {
                //配置不完整时整段子树按不可见处理，不画没有遮罩的替代结果。
                if (mask.GetDiagnostic().Length != 0) continue;
                //两个层次各走一遍，只有本遍真的有内容才入栈，避免空转出多余命令。
                if (SubtreeHasLayer(node, layer))
                {
                    clips.Push(mask);
                    PushClip(batch);
                    traversal.Add(null);
                }
            }

            if (node.Layer == layer) EmitNode(node, canvas, viewId, batch);

            //逆序入栈，出栈顺序才是原顺序。
            for (int index = node.Children.Count - 1; index >= 0; --index) traversal.Add(node.Children[index]);
        }
    }

    //输出一个节点上的图形：网格、矩阵与命中记录。
    private void EmitNode(UINode node, Canvas canvas, ulong viewId, CanvasBatch batch)
    {
        UIVisual? visual = node.Visual;
        if (visual == null || visual.RebuildFailed || node.ConfigurationError.Length != 0) return;
        if (!visual.IsUIActive()) return;
        //运行时隐藏只影响绘制与命中，布局照旧。
        if (visual.IsRuntimeHidden) return;

        UIMeshBuilder mesh = visual.Mesh;
        IReadOnlyList<UIMeshFragment> fragments = mesh.Fragments;
        if (fragments.Count == 0) return;

        int matrixIndex = AddMatrix(batch, node.CanvasMatrix);
        int objectId = visual.InstanceId;
        ulong revision = visual.MeshRevision;

        for (int index = 0; index < fragments.Count; ++index)
        {
            UIGeometryCache.MeshKey key = new(context.ManagedGeneration, objectId, index);
            bool changed = geometry.Acquire(key, revision, frameId, out ulong meshId);
            if (changed) AppendUpload(meshId, revision, mesh);
            //最终状态在本帧准备阶段算好，这里只取用，材质修改器不会跑第二遍。
            if (!visual.TryGetDrawState(index, out UIDrawState state)) state = fragments[index].state;
            AppendCommand(batch, meshId, fragments[index], state, matrixIndex);
        }

        if (visual.GetRaycastTarget())
            pendingHits.Add(new UIHitRecord(visual, canvas, node.Layout?.GetResolvedRect() ?? default,
                viewId, clips.Current));

        //控件的附加网格画在同节点图形之后、子节点之前。
        EmitControlOverlay(node, matrixIndex, batch);
    }

    //控件的附加网格：与图形共用同一个模型矩阵，片段独立上传与缓存。
    private void EmitControlOverlay(UINode node, int matrixIndex, CanvasBatch batch)
    {
        if (node.GetElement<UIControl>() is not UIControl control) return;
        if (control.GetOverlay() is not UIMeshBuilder overlay) return;

        IReadOnlyList<UIMeshFragment> fragments = overlay.Fragments;
        for (int index = 0; index < fragments.Count; ++index)
        {
            ulong revision = control.OverlayRevision;
            UIGeometryCache.MeshKey key = new(context.ManagedGeneration, control.InstanceId,
                OverlayFragmentBase + index);
            bool changed = geometry.Acquire(key, revision, frameId, out ulong meshId);
            if (changed) AppendUpload(meshId, revision, overlay);
            AppendCommand(batch, meshId, fragments[index], fragments[index].state, matrixIndex);
        }
    }

    //把栈顶裁剪层写成一条入栈命令；形状按矩形与 UV 生成一个四边形网格。
    private void PushClip(CanvasBatch batch)
    {
        if (!clips.TryPeekTop(out UIClipLayer layer)) return;

        clipShape.Clear();
        clipShape.SetTexture(layer.texture, UIMaterialKind.ImageStraight);
        bool flipY = layer.texture != null && !layer.texture.IsRenderTarget();
        clipShape.AddQuad(layer.rect, UIMeshBuilder.ToTextureUv(layer.uvMin, flipY),
            UIMeshBuilder.ToTextureUv(layer.uvMax, flipY), new color(1.0f, 1.0f, 1.0f, 1.0f));
        clipShape.Complete();

        UIGeometryCache.MeshKey key = new(context.ManagedGeneration, layer.ownerId,
            ClipFragmentBase + layer.slot);
        bool changed = geometry.Acquire(key, layer.revision, frameId, out ulong meshId);
        if (changed) AppendUpload(meshId, layer.revision, clipShape);

        batch.Commands.Add(new UIDrawCommand
        {
            meshId = meshId,
            //矩形裁剪不带纹理；Alpha 裁剪把纹理与阈值交给原生侧。
            textureObjectId = layer.mode == UIMaskMode.ImageAlpha ? layer.texture?.InstanceId ?? 0 : 0,
            materialKind = (uint)UIMaterialKind.ImageStraight,
            matrixIndex = (uint)AddMatrix(batch, layer.forward),
            commandKind = (uint)(layer.mode == UIMaskMode.ImageAlpha
                ? UIDrawCommandKind.PushImageAlpha
                : UIDrawCommandKind.PushRectangle),
            firstIndex = 0,
            indexCount = (uint)clipShape.Indices.Count,
            distanceRange = 1.0f,
            threshold = layer.threshold,
            tint = new color(1.0f, 1.0f, 1.0f, 1.0f),
        });
    }

    //弹出栈顶裁剪并补一条出栈命令。
    private void PopClip(CanvasBatch batch)
    {
        if (clips.IsEmpty) return;
        clips.Pop();
        batch.Commands.Add(new UIDrawCommand
        {
            meshId = 0,
            textureObjectId = 0,
            materialKind = 0,
            //出栈命令也要带一个合法矩阵下标，原生侧逐条校验。
            matrixIndex = 0,
            commandKind = (uint)UIDrawCommandKind.Pop,
            firstIndex = 0,
            indexCount = 0,
            distanceRange = 1.0f,
            threshold = 0.0f,
            tint = new color(1.0f, 1.0f, 1.0f, 1.0f),
        });
    }

    //节点是不是某个滚动容器的内容：是的话它的子树要按容器矩形裁剪。
    private static bool TryResolveScrollContent(UINode node, out UILayout? viewport)
    {
        viewport = null;
        UINode? parent = node.Parent;
        if (parent == null) return false;
        if (parent.GetElement<ScrollBox>() is not ScrollBox box) return false;
        if (box.GetContent() is not UILayout content) return false;
        //只裁直接子节点：间接子树由更靠内的容器负责。
        if (content.EnsId.id != node.Ens.id) return false;
        if (box.GetDiagnostic().Length != 0) return false;
        viewport = box.GetLayout();
        return viewport != null;
    }

    //本层子树里是否有属于这一遍的节点；含自身。
    private static bool SubtreeHasLayer(UINode node, int layer)
    {
        if (node.Layer == layer) return true;
        foreach (UINode child in node.Children)
        {
            if (SubtreeHasLayer(child, layer)) return true;
        }
        return false;
    }

    //把网格内容追加到本帧的上传缓冲，并登记一条更新。
    private void AppendUpload(ulong meshId, ulong revision, UIMeshBuilder mesh)
    {
        uint vertexOffset = (uint)uploadVertices.Count;
        uint indexOffset = (uint)uploadIndices.Count;
        uploadVertices.AddRange(mesh.Vertices);
        uploadIndices.AddRange(mesh.Indices);
        meshUpdates.Add(new UIMeshUpdate
        {
            meshId = meshId,
            revision = revision,
            vertexOffset = vertexOffset,
            vertexCount = (uint)mesh.Vertices.Count,
            indexOffset = indexOffset,
            indexCount = (uint)mesh.Indices.Count,
        });
    }

    //追加一条绘制命令；与上一条完全同源且索引连续时延长它，而不是新增一条。
    private static void AppendCommand(CanvasBatch batch, ulong meshId, in UIMeshFragment fragment,
        in UIDrawState state, int matrixIndex)
    {
        int textureObjectId = state.texture?.InstanceId ?? 0;
        ulong materialObjectId = state.material is Material materialRef && materialRef.InstanceId != 0
            ? unchecked((ulong)(uint)materialRef.InstanceId) : 0UL;
        if (batch.Commands.Count != 0)
        {
            UIDrawCommand previous = batch.Commands[^1];
            if (previous.commandKind == (uint)UIDrawCommandKind.Draw
                && previous.meshId == meshId
                && previous.textureObjectId == textureObjectId
                && previous.materialKind == (uint)state.materialKind
                && previous.materialObjectId == materialObjectId
                && previous.matrixIndex == (uint)matrixIndex
                && previous.distanceRange == state.distanceRange
                && SameTint(previous.tint, state.tint)
                && previous.firstIndex + previous.indexCount == (uint)fragment.firstIndex)
            {
                previous.indexCount += (uint)fragment.indexCount;
                batch.Commands[^1] = previous;
                return;
            }
        }

        batch.Commands.Add(new UIDrawCommand
        {
            meshId = meshId,
            textureObjectId = textureObjectId,
            materialKind = (uint)state.materialKind,
            matrixIndex = (uint)matrixIndex,
            commandKind = (uint)UIDrawCommandKind.Draw,
            firstIndex = (uint)fragment.firstIndex,
            indexCount = (uint)fragment.indexCount,
            distanceRange = state.distanceRange,
            threshold = 0.0f,
            tint = state.tint,
            materialObjectId = materialObjectId,
        });
    }

    //登记一个矩阵并返回它在画布数组里的下标；相同矩阵复用同一下标。
    private static int AddMatrix(CanvasBatch batch, in matrix4x4 value)
    {
        for (int index = 0; index < batch.Matrices.Count; ++index)
        {
            if (SameMatrix(batch.Matrices[index], value)) return index;
        }
        batch.Matrices.Add(value);
        return batch.Matrices.Count - 1;
    }

    private static bool SameTint(in color left, in color right) =>
        left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;

    private static bool SameMatrix(in matrix4x4 left, in matrix4x4 right)
    {
        for (int index = 0; index < 16; ++index)
        {
            if (left[index] != right[index]) return false;
        }
        return true;
    }
}
