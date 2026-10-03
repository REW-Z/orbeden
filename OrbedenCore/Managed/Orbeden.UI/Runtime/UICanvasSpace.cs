namespace Orbeden;

/// <summary>
/// 窗口坐标与画布/节点局部坐标之间的换算。命中检测与控件拖动共用同一套规则，
/// 分开实现迟早会出现"点得到却拖不动"这类不一致。
/// </summary>
internal static class UICanvasSpace
{
    /// <summary>沿祖先找所属画布。</summary>
    internal static Canvas? FindCanvas(UINode node)
    {
        for (UINode? current = node; current != null; current = current.Parent)
        {
            if (current.Canvas != null) return current.Canvas;
        }
        return null;
    }

    /// <summary>
    /// 窗口逻辑点换算到画布逻辑坐标：按显示缩放换到画布像素，再按画布缩放换到逻辑，最后翻转 Y。
    /// </summary>
    internal static bool TryWindowToCanvas(UINode node, in UIView view, vector2 windowPoint, out vector2 canvasPoint)
    {
        canvasPoint = default;
        Canvas? canvas = FindCanvas(node);
        if (canvas == null || view.width <= 0 || view.height <= 0) return false;
        if (view.logicalSize.x <= 0.0f || view.logicalSize.y <= 0.0f) return false;

        vector2 pixelSize = new(view.width, view.height);
        vector2 logicalSize = canvas.ComputeLogicalSize(pixelSize);
        float scale = canvas.ComputeScale(pixelSize);
        if (logicalSize.x <= 0.0f || logicalSize.y <= 0.0f || scale <= 0.0f) return false;

        float displayScale = view.width / view.logicalSize.x;
        float x = (windowPoint.x - view.logicalOrigin.x) * displayScale / scale;
        float y = logicalSize.y - (windowPoint.y - view.logicalOrigin.y) * displayScale / scale;
        canvasPoint = new vector2(x, y);
        return true;
    }

    /// <summary>画布逻辑坐标换算到节点局部坐标；奇异矩阵返回假。</summary>
    internal static bool TryCanvasToLocal(UINode node, vector2 canvasPoint, out vector2 localPoint)
    {
        localPoint = default;
        if (!UIMatrix.TryInvertAffine(UIMatrix.Compose(node), out matrix4x4 inverse)) return false;
        localPoint = UIMatrix.TransformPoint(inverse, canvasPoint);
        return true;
    }

    /// <summary>窗口逻辑点换算到节点局部坐标。</summary>
    internal static bool TryWindowToLocal(UINode node, in UIView view, vector2 windowPoint, out vector2 localPoint)
    {
        localPoint = default;
        if (!TryWindowToCanvas(node, view, windowPoint, out vector2 canvasPoint)) return false;
        return TryCanvasToLocal(node, canvasPoint, out localPoint);
    }

    /// <summary>
    /// 取与视图标识对应的快照；没有上下文或视图时返回假。
    /// 世界空间下同一块画布每台相机各有一条快照，观察者标识用来挑出对的那条。
    /// </summary>
    internal static bool TryGetView(ulong viewId, ulong viewerId, out UIView view)
    {
        view = default;
        UIWorldContext? context = UIWorldContext.Current;
        if (context?.NativeBridge is not RetainedGuiBridge bridge) return false;

        UIView? fallback = null;
        foreach (UIView candidate in bridge.ReadViews(context.ViewBuffer))
        {
            if (candidate.viewId != viewId) continue;
            if (candidate.viewerId == viewerId)
            {
                view = candidate;
                return true;
            }
            //观察者对不上时留一条同画布的备用；相机被删掉后命中不该整块失效。
            fallback ??= candidate;
        }
        if (fallback == null) return false;
        view = fallback.Value;
        return true;
    }

    /// <summary>节点所属画布的视图快照；一台相机有多条时取不限定相机的那条或第一条。</summary>
    internal static bool TryGetCanvasView(UINode node, out UIView view)
    {
        view = default;
        Canvas? canvas = FindCanvas(node);
        if (canvas == null) return false;
        return TryGetView(unchecked((ulong)(uint)canvas.InstanceId), 0UL, out view);
    }
}
