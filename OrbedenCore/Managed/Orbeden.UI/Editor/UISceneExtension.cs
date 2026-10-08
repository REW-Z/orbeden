using System;
using Orbeden;
using Numerics = System.Numerics;

namespace OrbedenEditor;

/// <summary>Canvas 常显边框及 UI 场景射线拾取。</summary>
internal sealed class UISceneExtension : IEditorSceneExtension
{
    private readonly UITextureAlphaCache alpha = new();

    /// <summary>绘制活动画布的场景边框。</summary>
    public void DrawOverlay()
    {
        UIWorldContext? context = UIWorldContext.Current;
        if (context == null || !context.IsEditorMode) return;
        foreach (Canvas canvas in context.Canvases)
        {
            if (!canvas.IsUIActive() || canvas.GetLayout() is not UILayout layout) continue;
            UILayoutGizmos.DrawOutline(layout, UILayoutGizmos.BorderTint(Gizmos.IsSelected(canvas.EnsId)));
        }
    }

    /// <summary>选择未被网格遮挡的 UI，优先更深的 Ens 节点。</summary>
    public bool TryPick(in EditorScenePickRay ray, out EditorScenePickHit hit)
    {
        hit = default;
        UIWorldContext? context = UIWorldContext.Current;
        if (context == null || !context.IsEditorMode) return false;
        int bestDepth = -1;
        float bestDistance = float.PositiveInfinity;
        foreach (UINode node in context.Nodes)
        {
            UIVisual? visual = node.Visual;
            if (!node.IsRenderable || node.Layout == null || visual == null || visual.RebuildFailed
                || !visual.IsUIActive() || visual.IsRuntimeHidden || visual.GetComposedTint().a <= 0) continue;
            if (visual.GetCanvas()?.GetRenderMode() == CanvasRenderMode.Offscreen) continue;
            if (!UILayoutGizmos.TryGetSceneMatrix(node, out matrix4x4 matrix)) continue;

            //求射线与实际预览平面的交点
            vector3 normal = new(matrix[1] * matrix[6] - matrix[2] * matrix[5],
                matrix[2] * matrix[4] - matrix[0] * matrix[6], matrix[0] * matrix[5] - matrix[1] * matrix[4]);
            float denominator = normal.x * ray.Direction.x + normal.y * ray.Direction.y + normal.z * ray.Direction.z;
            if (!float.IsFinite(denominator) || MathF.Abs(denominator) < 1e-10f) continue;
            float distance = ((matrix[12] - ray.Origin.x) * normal.x + (matrix[13] - ray.Origin.y) * normal.y
                + (matrix[14] - ray.Origin.z) * normal.z) / denominator;
            if (!float.IsFinite(distance) || distance < 0 || distance > ray.MaximumDistance + 1e-4f) continue;
            vector3 point = new(ray.Origin.x + ray.Direction.x * distance,
                ray.Origin.y + ray.Direction.y * distance, ray.Origin.z + ray.Direction.z * distance);
            if (!TryGetLocalPoint(matrix, point, out vector2 local) || !node.Layout.GetResolvedRect().ContainsInclusive(local)) continue;
            if (!IsVisibleThroughMasks(node, point)) continue;

            //按 Ens 层级深度和相机距离排序
            int depth = 0;
            for (Ens owner = visual.Ens; owner.IsValid; owner = Ens.FromId(owner.Transform.GetParent())) ++depth;
            if (depth < bestDepth || depth == bestDepth && distance > bestDistance + 1e-4f) continue;
            hit = new EditorScenePickHit { Ens = node.Ens, Position = point };
            bestDepth = depth;
            bestDistance = distance;
        }
        return bestDepth >= 0;
    }

    //检查祖先遮罩对交点的覆盖率
    private bool IsVisibleThroughMasks(UINode node, vector3 point)
    {
        float coverage = 1;
        for (UINode? current = node; current != null; current = current.Parent)
        {
            Mask? mask = current.GetElement<Mask>();
            if (mask == null) continue;
            if (mask.GetDiagnostic().Length != 0 || current.Layout == null
                || !UILayoutGizmos.TryGetSceneMatrix(current, out matrix4x4 matrix)
                || !TryGetLocalPoint(matrix, point, out vector2 local)) return false;
            UIRect rect = current.Layout.GetResolvedRect();
            if (rect.Width <= 0 || rect.Height <= 0 || !rect.ContainsInclusive(local)) return false;
            if (mask.GetMode() == UIMaskMode.ImageAlpha)
            {
                vector2 min = mask.GetUvMin(), max = mask.GetUvMax();
                float u = min.x + (local.x - rect.min.x) / rect.Width * (max.x - min.x);
                float v = min.y + (local.y - rect.min.y) / rect.Height * (max.y - min.y);
                coverage *= alpha.Sample(mask.GetTexture(), u, v);
                if (coverage <= 0 || coverage < mask.GetHitTestThreshold()) return false;
            }
        }
        return true;
    }

    //将世界交点变换回布局局部空间
    private static bool TryGetLocalPoint(in matrix4x4 matrix, vector3 point, out vector2 local)
    {
        local = default;
        Numerics.Matrix4x4 value = new(matrix[0], matrix[1], matrix[2], matrix[3],
            matrix[4], matrix[5], matrix[6], matrix[7], matrix[8], matrix[9], matrix[10], matrix[11],
            matrix[12], matrix[13], matrix[14], matrix[15]);
        if (!Numerics.Matrix4x4.Invert(value, out Numerics.Matrix4x4 inverse)) return false;
        Numerics.Vector3 position = Numerics.Vector3.Transform(new Numerics.Vector3(point.x, point.y, point.z), inverse);
        local = new vector2(position.X, position.Y);
        return float.IsFinite(local.x) && float.IsFinite(local.y);
    }
}
