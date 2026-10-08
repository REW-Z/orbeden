using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 命中检测。只用最近一次成功呈现的快照：视图尺寸、裁剪层与图形顺序都来自同一呈现序号，
/// 因此命中的东西一定就是屏幕上画出来的东西。世界空间还要与同一序号的场景深度比较。
/// </summary>
public sealed class UIRaycaster
{
    //NDC 深度转窗口深度的容差；浮点比较不做精确相等。
    private const float DepthTolerance = 1e-5f;

    private readonly UIFrameBuilder frameBuilder;
    private readonly UIWorldContext context;
    //一次输入阶段内同像素的深度只回读一次。
    private readonly Dictionary<(ulong viewId, ulong viewerId, ulong frame, int x, int y), float> depthCache = [];

    internal UIRaycaster(UIWorldContext context, UIFrameBuilder frameBuilder)
    {
        this.context = context;
        this.frameBuilder = frameBuilder;
    }

    /// <summary>清空深度回读缓存；每次输入阶段开始时调用。</summary>
    public void ResetDepthCache() => depthCache.Clear();

    /// <summary>
    /// 判定指针命中的图形与控件。屏幕画布优先于世界空间画布；
    /// 同一视图内按绘制顺序逆序，最先命中的就是最上面的那个。
    /// </summary>
    public bool Raycast(in UIPointerEvent input, out UIHitResult result)
    {
        result = default;
        ulong presentedFrame = frameBuilder.PresentedFrame;
        //首次呈现之前指针不命中：还没有可以对照的画面。
        if (presentedFrame == 0) return false;

        IReadOnlyList<UIHitRecord> hits = frameBuilder.PresentedHits;
        //第一遍：屏幕与离屏画布。
        for (int index = hits.Count - 1; index >= 0; --index)
        {
            if (!TryResolve(hits[index], out UINode node, out bool worldSpace) || worldSpace) continue;
            if (TryHitCanvasSpace(hits[index], node, input, presentedFrame, out result)) return true;
        }
        //按相机呈现顺序检测共享世界空间网格
        if (context.NativeBridge is not RetainedGuiBridge bridge) return false;
        Span<UIView> views = bridge.ReadViews(context.ViewBuffer);
        for (int viewIndex = views.Length - 1; viewIndex >= 0; --viewIndex)
        {
            UIView view = views[viewIndex];
            if ((view.flags & UIViewFlags.WorldSpaceCamera) == 0 || view.presentedFrame != presentedFrame) continue;
            for (int index = hits.Count - 1; index >= 0; --index)
            {
                if (hits[index].viewId != view.viewId) continue;
                if (!TryResolve(hits[index], out UINode node, out bool worldSpace) || !worldSpace) continue;
                if (TryHitWorldSpace(hits[index], node, view, input, presentedFrame, out result)) return true;
            }
        }
        return false;
    }

    /// <summary>沿祖先寻找最近的控件；没有控件时返回空，但命中本身仍然消费指针序列。</summary>
    public static UIControl? FindControl(UINode node)
    {
        for (UINode? current = node; current != null; current = current.Parent)
        {
            UIControl? control = current.GetElement<UIControl>();
            if (control != null) return control;
        }
        return null;
    }

    //屏幕与离屏画布：把窗口逻辑点换算到画布逻辑坐标，再判局部矩形与裁剪层。
    private bool TryHitCanvasSpace(in UIHitRecord record, UINode node, in UIPointerEvent input,
        ulong presentedFrame, out UIHitResult result)
    {
        result = default;
        if (record.visual is not UIVisual visual) return false;
        if (!UICanvasSpace.TryGetView(record.viewId, 0, out UIView view)) return false;
        if (!UICanvasSpace.TryWindowToCanvas(node, view, input.position, out vector2 canvasPoint)) return false;
        if (!record.clips.TestPoint(canvasPoint)) return false;
        if (!UICanvasSpace.TryCanvasToLocal(node, canvasPoint, out vector2 localPoint)) return false;
        if (!visual.Raycast(localPoint)) return false;
        //图形与祖先上的命中过滤器：任一否决就排除这个候选，继续看下层图形。
        if (!CheckRaycastFilters(node, canvasPoint)) return false;

        result.visual = visual;
        result.canvas = record.canvas;
        result.control = FindControl(node);
        result.localPoint = localPoint;
        result.presentedFrame = presentedFrame;
        return true;
    }

    //世界空间画布：反投影相机射线，与图形所在平面求交，再与已呈现的场景深度比较。
    private bool TryHitWorldSpace(in UIHitRecord record, UINode node, in UIView view, in UIPointerEvent input,
        ulong presentedFrame, out UIHitResult result)
    {
        result = default;
        if (record.visual is not UIVisual visual) return false;

        matrix4x4 viewProjection = matrix4x4.Multiply(view.projection, view.view);
        if (!UIMatrix.TryInvert(viewProjection, out matrix4x4 inverseViewProjection)) return false;
        if (!TryWindowToNdc(view, input.position, out vector2 ndc, out vector2 pixel)) return false;

        //近平面与远平面各反投影一次，连成射线。
        if (!UIMatrix.TryTransformHomogeneous(inverseViewProjection, new vector3(ndc.x, ndc.y, -1.0f), out vector3 near))
            return false;
        if (!UIMatrix.TryTransformHomogeneous(inverseViewProjection, new vector3(ndc.x, ndc.y, 1.0f), out vector3 far))
            return false;

        vector3 direction = new(far.x - near.x, far.y - near.y, far.z - near.z);
        matrix4x4 toWorld = UIMatrix.ComposeIncludingCanvas(node);
        //图形位于自身局部 z=0 平面：法线是局部 Z 轴变换到世界后的方向。
        vector3 normal = new(toWorld[8], toWorld[9], toWorld[10]);
        float normalLength = MathF.Sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (!float.IsFinite(normalLength) || normalLength < 1e-6f) return false;
        normal = new vector3(normal.x / normalLength, normal.y / normalLength, normal.z / normalLength);

        vector3 planePoint = new(toWorld[12], toWorld[13], toWorld[14]);
        float denominator = direction.x * normal.x + direction.y * normal.y + direction.z * normal.z;
        //射线与平面平行：没有交点。
        if (!float.IsFinite(denominator) || MathF.Abs(denominator) < 1e-8f) return false;

        float t = ((planePoint.x - near.x) * normal.x + (planePoint.y - near.y) * normal.y
            + (planePoint.z - near.z) * normal.z) / denominator;
        //交点在相机背后：不算命中。
        if (!float.IsFinite(t) || t < 0.0f) return false;

        vector3 world = new(near.x + direction.x * t, near.y + direction.y * t, near.z + direction.z * t);
        if (!UIMatrix.TryInvertAffine(toWorld, out matrix4x4 worldToLocal)) return false;
        if (!UIMatrix.TryTransformHomogeneous(worldToLocal, world, out vector3 localPosition)) return false;
        vector2 local = new(localPosition.x, localPosition.y);
        if (!visual.Raycast(local)) return false;
        vector2 canvasPoint = UIMatrix.TransformPoint(node.CanvasMatrix, local);
        if (!record.clips.TestPoint(canvasPoint)) return false;
        //过滤器在世界空间命中也一样生效；坐标按画布空间换算到各节点局部。
        if (!CheckRaycastFilters(node, canvasPoint)) return false;

        //再与已呈现的场景深度比较：读不到深度就不允许这个候选命中。
        if (!TryReadDepth(record.viewId, view.viewerId, presentedFrame, (int)pixel.x, view.height - 1 - (int)pixel.y,
            out float sceneDepth)) return false;
        if (!TryProjectDepth(viewProjection, world, out float ndcDepth)) return false;
        if (ndcDepth > sceneDepth + DepthTolerance) return false;

        result.visual = visual;
        result.canvas = record.canvas;
        result.control = FindControl(node);
        result.localPoint = local;
        result.presentedFrame = presentedFrame;
        return true;
    }


    //图形及其祖先上的命中过滤器：画布空间的点换算到每个过滤器所在节点的局部空间。
    private bool CheckRaycastFilters(UINode node, vector2 canvasPoint)
    {
        for (UINode? current = node; current != null; current = current.Parent)
        {
            IUIRaycastFilter? filter = GetRaycastFilter(current.Ens);
            if (filter == null) continue;

            vector2 local = canvasPoint;
            if (!ReferenceEquals(current, node))
            {
                //父节点的局部空间：画布点经它自己的逆变换换进去。
                if (!UIMatrix.TryInvertAffine(UIMatrix.Compose(current), out matrix4x4 inverse)) return false;
                local = UIMatrix.TransformPoint(inverse, canvasPoint);
            }
            if (!filter.IsRaycastLocationValid(local)) return false;
        }
        return true;
    }

    //节点上第一个启用的命中过滤器；没有时返回空。
    private IUIRaycastFilter? GetRaycastFilter(EnsId ens)
    {
        foreach (Script script in context.HandlerCache.GetHandlers(ens))
        {
            if (!script.GetEnabled()) continue;
            if (script is IUIRaycastFilter filter) return filter;
        }
        return null;
    }

    //窗口逻辑点换算到视图 NDC；同时给出像素坐标供深度回读用。
    private static bool TryWindowToNdc(in UIView view, vector2 windowPoint, out vector2 ndc, out vector2 pixel)
    {
        ndc = default;
        pixel = default;
        if (view.width <= 0 || view.height <= 0) return false;
        if (view.logicalSize.x <= 0.0f || view.logicalSize.y <= 0.0f) return false;

        //原点在缩放之前扣掉：logicalOrigin 本身就是窗口逻辑坐标。
        float x = (windowPoint.x - view.logicalOrigin.x) * view.width / view.logicalSize.x;
        float y = (windowPoint.y - view.logicalOrigin.y) * view.height / view.logicalSize.y;
        pixel = new vector2(x, y);

        //视图之外直接不命中。
        if (x < 0.0f || y < 0.0f || x >= view.width || y >= view.height) return false;
        ndc = new vector2(x / view.width * 2.0f - 1.0f, 1.0f - y / view.height * 2.0f);
        return true;
    }

    //世界点投影回裁剪空间，取 NDC 深度。
    private static bool TryProjectDepth(in matrix4x4 viewProjection, vector3 world, out float ndcDepth)
    {
        ndcDepth = 0.0f;
        float clipW = viewProjection[3] * world.x + viewProjection[7] * world.y + viewProjection[11] * world.z + viewProjection[15];
        float clipZ = viewProjection[2] * world.x + viewProjection[6] * world.y + viewProjection[10] * world.z + viewProjection[14];
        if (!float.IsFinite(clipW) || MathF.Abs(clipW) < 1e-8f) return false;
        ndcDepth = (clipZ / clipW + 1.0f) * 0.5f;
        return true;
    }

    /// <summary>读取已呈现帧的窗口深度；同一输入阶段同像素只回读一次。</summary>
    public bool TryReadDepth(ulong viewId, ulong viewerId, ulong presentedFrame, int x, int y, out float depth)
    {
        depth = 0.0f;
        var key = (viewId, viewerId, presentedFrame, x, y);
        if (depthCache.TryGetValue(key, out float cached))
        {
            depth = cached;
            return true;
        }

        if (context.NativeBridge is not RetainedGuiBridge bridge) return false;
        if (!bridge.ReadDepth(viewId, viewerId, presentedFrame, x, y, out depth)) return false;

        depthCache[key] = depth;
        return true;
    }

    //取记录对应的节点，并判断它属于屏幕类还是世界空间。
    private bool TryResolve(in UIHitRecord record, out UINode node, out bool worldSpace)
    {
        node = null!;
        worldSpace = false;
        if (record.visual == null) return false;

        UINode? found = context.FindNode(record.visual.EnsId);
        if (found == null || !found.IsRenderable) return false;

        node = found;
        worldSpace = UICanvasSpace.FindCanvas(found)?.GetRenderMode() == CanvasRenderMode.WorldSpace;
        return true;
    }



}
