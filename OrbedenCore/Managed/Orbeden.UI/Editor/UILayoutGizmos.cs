using System;
using Orbeden;
using OrbedenEditor;

namespace OrbedenEditor;

/// <summary>
/// 布局手柄。矩形编辑按"开始时的快照"反算字段，绝不累计中间拖动值；
/// 一次拖动在结束时合成一条历史；被驱动的轴只读并提示 owner。
/// 场景交互走 <see cref="OnSceneGui"/>：矩形轮廓与四个角点、一个枢轴点都投到屏幕上画，
/// 投影比例在按下的那一帧锁定，拖动量先由屏幕位移换算回布局空间，再按基线反算，
/// 因此拖动过程只读基线、不累加中间值。
/// 增量接口（ApplyOffsetDelta/ApplySizeDelta/ApplyPivotKeepRect）留给面板等其它入口复用。
/// </summary>
public sealed class UILayoutGizmos
{
    //手柄槽位：0..3 是四个角，4..7 是四条边中点，8 是枢轴点（拖动它整体位移）。
    private const int LeftEdge = 4;
    private const int TopEdge = 5;
    private const int RightEdge = 6;
    private const int BottomEdge = 7;
    private const int PivotHandle = 8;
    private const int HandleCount = 9;

    //场景里的配色：边框未选中为白色、选中转绿；矩形手柄保持青色系，避免和原生变换手柄混淆。
    private static readonly color borderColor = new(1.0f, 1.0f, 1.0f, 0.8f);
    private static readonly color selectedBorderColor = new(0.35f, 0.9f, 0.45f, 1.0f);
    private static readonly color cornerColor = new(0.35f, 0.78f, 1.0f);
    private static readonly color pivotColor = new(1.0f, 0.78f, 0.25f);

    //拖动开始时的布局快照；null 表示当前没有进行中的编辑。
    private LayoutBaseline? baseline;
    //本次编辑是否已经提示过被驱动的轴。
    private bool reportedDrivenAxis;
    //正在拖动的角点（0..3）或枢轴（4）；-1 表示没在拖。
    private int activeHandle = -1;
    //按下那一帧的鼠标屏幕坐标，以及轴角点在布局空间的单位长度折合多少屏幕像素。
    private vector2 dragStartMouse;
    private vector2 screenPerUnitX;
    private vector2 screenPerUnitY;
    //投影结果复用同一块缓冲：手柄每帧都要重算，不该每次都分配。
    private readonly vector2[] handleScreens = new vector2[HandleCount];
    private readonly bool[] handleVisible = new bool[HandleCount];

    //轮廓的四个世界角点；同上，静态共享即可，绘制期间不跨线程。
    private static readonly vector3[] outlineCorners = new vector3[5];

    /// <summary>当前是否正在编辑矩形。</summary>
    public bool IsEditing => baseline != null;

    /// <summary>开始一次矩形编辑；重复调用只保留第一次的基线。</summary>
    public void Begin(UILayout layout)
    {
        if (baseline != null || layout == null) return;
        baseline = LayoutBaseline.Capture(layout);
        reportedDrivenAxis = false;
    }

    /// <summary>按增量移动矩形：写的是 offset，锚点与尺寸不动。</summary>
    public void ApplyOffsetDelta(vector2 delta)
    {
        if (baseline == null || delta.x == 0.0f && delta.y == 0.0f) return;
        if (!TryResolve(out UILayout? layout, out LayoutBaseline start)) return;

        //驱动轴只读：偏移由别人写，这里不覆盖，只在首次提示。
        if (IsDriven(layout))
        {
            ReportDrivenAxis(layout);
            return;
        }
        if (layout.Ens.GetComponent<Canvas>() is Canvas)
        {
            matrix4x4 matrix = start.LocalMatrix;
            layout.Ens.Transform.SetLocalPosition(new vector3(start.Position.x + matrix[0] * delta.x + matrix[4] * delta.y,
                start.Position.y + matrix[1] * delta.x + matrix[5] * delta.y,
                start.Position.z + matrix[2] * delta.x + matrix[6] * delta.y));
        }
        else layout.SetOffset(new vector2(start.Offset.x + delta.x, start.Offset.y + delta.y));
    }

    /// <summary>按增量调整尺寸：写的是 sizeDelta，偏移不动。</summary>
    public void ApplySizeDelta(vector2 delta)
    {
        if (baseline == null || delta.x == 0.0f && delta.y == 0.0f) return;
        if (!TryResolve(out UILayout? layout, out LayoutBaseline start)) return;

        layout.SetSizeDelta(new vector2(start.SizeDelta.x + delta.x, start.SizeDelta.y + delta.y));
    }

    /// <summary>
    /// 保持矩形不变地改枢轴：按起始矩形的公式重算偏移，
    /// 因此连续拖动不会把误差累进 offset。
    /// </summary>
    public void ApplyPivotKeepRect(vector2 pivot)
    {
        if (!TryResolve(out UILayout? layout, out LayoutBaseline start)) return;

        //按锚点跨度抵消后的尺寸增量补偿轴心偏移
        layout.SetPivot(pivot);
        vector2 next = layout.GetPivot();
        layout.SetOffset(new vector2(start.Offset.x + (next.x - start.Pivot.x) * start.SizeDelta.x,
            start.Offset.y + (next.y - start.Pivot.y) * start.SizeDelta.y));
    }

    /// <summary>结束编辑：把基线到当前值的差别合成一条历史。</summary>
    public void End(string label = "UI Layout")
    {
        if (baseline == null) return;
        LayoutBaseline start = baseline.Value;
        baseline = null;
        if (!start.TryResolve(out UILayout? layout) || layout == null) return;

        LayoutBaseline end = LayoutBaseline.Capture(layout);
        if (start.Matches(end)) return;

        UILayout target = layout;
        EditorPropertyHistory.RecordAction(label,
            () => start.Apply(target), () => end.Apply(target));
    }

    /// <summary>取消编辑：解析回基线，不写历史。</summary>
    public void Cancel()
    {
        if (baseline == null) return;
        LayoutBaseline start = baseline.Value;
        baseline = null;
        if (start.TryResolve(out UILayout? layout) && layout != null) start.Apply(layout);
    }

    /// <summary>
    /// 场景里绘制布局矩形并处理拖动：四个角改尺寸、枢轴点整体位移。
    /// 只在 SceneView 的 OnSceneGui 上下文里调用；相机或节点不可用时只收尾、不抛。
    /// </summary>
    public void OnSceneGui(UILayout layout)
    {
        if (layout == null) return;
        UINode? node = UIWorldContext.Current?.FindNode(layout.EnsId);
        if (node == null)
        {
            //节点没了：基线指向的对象已经无效，直接丢弃，不写历史。
            Cancel();
            return;
        }

        //子节点边框只在选中时画，因此这里恒取到选中色；画布的常显边框由场景扩展用同一套配色画。
        if (node.Canvas == null) DrawOutline(layout, BorderTint(Gizmos.IsSelected(node.Ens)));

        //按 Rect 模式启用矩形交互
        if (EditorSceneHandles.Mode != EditorSceneHandleMode.Rect)
        {
            activeHandle = -1;
            Cancel();
            return;
        }

        if (node.Canvas?.GetRenderMode() == CanvasRenderMode.Offscreen) return;

        //投影失败说明相机或视口不可用：轮廓画得出来，但拖拽区没有落点。
        if (!TryProjectHandles(node, layout)) return;

        //同一帧最多只有一个条目处于活动状态，这里只记下被按住的那一个。
        int dragging = -1;
        for (int handle = 0; handle < handleScreens.Length; ++handle)
        {
            if (!handleVisible[handle]) continue;
            //保留屏幕画布的移动手柄
            if (node.Canvas?.GetRenderMode() == CanvasRenderMode.Overlay && handle != PivotHandle) continue;
            EditorGUI.PushId($"ui_layout_handle_{handle}");
            try
            {
                float size = handle == PivotHandle ? EditorSceneHandles.DefaultHandleSize + 2.0f
                    : EditorSceneHandles.DefaultHandleSize;
                color tint = handle == PivotHandle ? pivotColor : cornerColor;
                if (EditorSceneHandles.Draw("##handle", handleScreens[handle], tint, size)
                    == EditorSceneHandleState.Dragging)
                {
                    dragging = handle;
                }
            }
            finally
            {
                EditorGUI.PopId();
            }
        }

        //拖动中途按 Escape 回到基线，并且不写历史。
        if (activeHandle >= 0 && EditorInput.IsKeyPressed(EditorKey.Escape))
        {
            activeHandle = -1;
            Cancel();
            return;
        }

        if (dragging < 0)
        {
            //松手那一帧合成历史；基线只在这里结算一次。
            if (activeHandle >= 0)
            {
                activeHandle = -1;
                End();
            }
            return;
        }

        if (activeHandle < 0) BeginHandleDrag(layout, node, dragging);
        ApplyHandleDrag(layout);
    }

    /// <summary>边框配色：未选中为白色，选中转绿。</summary>
    public static color BorderTint(bool selected) => selected ? selectedBorderColor : borderColor;

    /// <summary>在场景里画布局矩形；使用解析后的世界矩阵。</summary>
    public static void DrawOutline(UILayout layout, color tint)
    {
        UINode? node = UIWorldContext.Current?.FindNode(layout.EnsId);
        if (node == null) return;

        UIRect rect = layout.GetResolvedRect();
        //复用点数组并批量提交矩形轮廓
        if (!TryGetSceneMatrix(node, out matrix4x4 matrix)) return;
        vector3[] corners = outlineCorners;
        corners[0] = ToScenePoint(matrix, rect.min);
        corners[1] = ToScenePoint(matrix, new vector2(rect.Max.x, rect.min.y));
        corners[2] = ToScenePoint(matrix, rect.Max);
        corners[3] = ToScenePoint(matrix, new vector2(rect.min.x, rect.Max.y));
        corners[4] = corners[0];
        Gizmos.PolyLine(corners, tint);
    }

    //独立投影各个手柄并记录可见性
    private bool TryProjectHandles(UINode node, UILayout layout)
    {
        UIRect rect = layout.GetResolvedRect();

        //角点顺序与 ApplyResizeDelta 的符号表一致：0 左上、1 右上、2 右下、3 左下，
        //接着是四条边的中点，最后是枢轴。
        float centerX = (rect.min.x + rect.Max.x) * 0.5f;
        float centerY = (rect.min.y + rect.Max.y) * 0.5f;
        handleScreens[0] = new vector2(rect.min.x, rect.Max.y);
        handleScreens[1] = new vector2(rect.Max.x, rect.Max.y);
        handleScreens[2] = new vector2(rect.Max.x, rect.min.y);
        handleScreens[3] = new vector2(rect.min.x, rect.min.y);
        handleScreens[LeftEdge] = new vector2(rect.min.x, centerY);
        handleScreens[TopEdge] = new vector2(centerX, rect.Max.y);
        handleScreens[RightEdge] = new vector2(rect.Max.x, centerY);
        handleScreens[BottomEdge] = new vector2(centerX, rect.min.y);
        handleScreens[PivotHandle] = new vector2(centerX, centerY);

        bool anyVisible = false;
        for (int handle = 0; handle < handleScreens.Length; ++handle)
        {
            handleVisible[handle] = TryProjectLayoutPoint(node, handleScreens[handle], out vector2 screen);
            if (!handleVisible[handle]) continue;
            handleScreens[handle] = screen;
            anyVisible = true;
        }
        return anyVisible;
    }

    //按下时锁定基线、鼠标起点与投影比例：拖动过程中相机不动，这三个量全程有效。
    private void BeginHandleDrag(UILayout layout, UINode node, int handle)
    {
        Begin(layout);
        activeHandle = handle;
        dragStartMouse = EditorSceneHandles.MousePosition;

        UIRect rect = layout.GetResolvedRect();
        UINode projectionNode = handle == PivotHandle && node.Canvas == null && node.Parent != null ? node.Parent : node;
        vector2 origin = new(rect.min.x, rect.min.y);
        screenPerUnitX = default;
        screenPerUnitY = default;

        //三个点都投得出来才拿得到比例；少一个就保持零矩阵，下面的反算会直接放弃。
        bool projected = TryProjectLayoutPoint(projectionNode, origin, out vector2 baseScreen);
        if (projected)
        {
            projected = TryProjectLayoutPoint(projectionNode, new vector2(origin.x + 1.0f, origin.y), out vector2 unitX);
            if (projected)
            {
                screenPerUnitX = new vector2(unitX.x - baseScreen.x, unitX.y - baseScreen.y);
                projected = TryProjectLayoutPoint(projectionNode, new vector2(origin.x, origin.y + 1.0f), out vector2 unitY);
                if (projected)
                {
                    screenPerUnitY = new vector2(unitY.x - baseScreen.x, unitY.y - baseScreen.y);
                }
            }
        }
    }

    //把从按下到现在的屏幕位移换算回布局空间，再从基线反算字段。
    private void ApplyHandleDrag(UILayout layout)
    {
        vector2 mouse = EditorSceneHandles.MousePosition;
        vector2 screenDelta = new(mouse.x - dragStartMouse.x, mouse.y - dragStartMouse.y);
        if (!TryScreenDeltaToLayout(screenDelta, out vector2 delta)) return;
        if (delta.x == 0.0f && delta.y == 0.0f) return;

        if (activeHandle == PivotHandle)
        {
            ApplyOffsetDelta(delta);
            return;
        }
        //边中点只动一个轴，另一个轴传 0 表示不参与。
        (float signX, float signY) = HandleSigns(activeHandle);
        ApplyResizeDelta(signX, signY, delta);
    }

    //手柄的符号表：正号动最大边，负号动最小边，0 表示这一轴不动。
    private static (float SignX, float SignY) HandleSigns(int handle) => handle switch
    {
        0 => (-1.0f, 1.0f),
        1 => (1.0f, 1.0f),
        2 => (1.0f, -1.0f),
        3 => (-1.0f, -1.0f),
        LeftEdge => (-1.0f, 0.0f),
        TopEdge => (0.0f, 1.0f),
        RightEdge => (1.0f, 0.0f),
        BottomEdge => (0.0f, -1.0f),
        _ => (0.0f, 0.0f),
    };

    //解二元一次方程组：屏幕位移 = 单位向量矩阵 × 布局位移。
    private bool TryScreenDeltaToLayout(vector2 screenDelta, out vector2 delta)
    {
        float determinant = screenPerUnitX.x * screenPerUnitY.y - screenPerUnitY.x * screenPerUnitX.y;
        //画布几乎侧对相机时两个单位向量共线，这时反算没有意义，宁可不改。
        if (MathF.Abs(determinant) <= 1e-6f)
        {
            delta = default;
            return false;
        }

        delta = new vector2(
            (screenPerUnitY.y * screenDelta.x - screenPerUnitY.x * screenDelta.y) / determinant,
            (screenPerUnitX.x * screenDelta.y - screenDelta.x * screenPerUnitX.y) / determinant);
        return true;
    }

    /// <summary>按角点拖动改矩形，见 <see cref="ApplyResizeDelta"/>。角点下标 0..3。</summary>
    public void ApplyCornerDelta(int corner, vector2 delta)
    {
        if (corner < 0 || corner > 3) return;
        (float signX, float signY) = HandleSigns(corner);
        ApplyResizeDelta(signX, signY, delta);
    }

    /// <summary>
    /// 按边缘拖动改矩形：被拖动的边跟着走，对边保持不动，因此尺寸与偏移一起改。
    /// signX/signY 取 1 动最大边、-1 动最小边、0 表示这一轴不动（边中点手柄只动一个轴）。
    /// 与其它入口一样从基线反算，拖动中途不会累积误差。
    /// </summary>
    public void ApplyResizeDelta(float signX, float signY, vector2 delta)
    {
        if (baseline == null) return;
        if (signX == 0.0f && signY == 0.0f) return;
        if (!TryResolve(out UILayout? layout, out LayoutBaseline start)) return;
        if (IsDriven(layout))
        {
            ReportDrivenAxis(layout);
            return;
        }

        //夹紧拖动尺寸并跳过内容驱动的轴
        if (layout.GetFitWidth()) signX = 0;
        if (layout.GetFitHeight()) signY = 0;
        if (signX > 0) delta.x = MathF.Max(delta.x, -start.ResolvedRect.Width);
        else if (signX < 0) delta.x = MathF.Min(delta.x, start.ResolvedRect.Width);
        if (signY > 0) delta.y = MathF.Max(delta.y, -start.ResolvedRect.Height);
        else if (signY < 0) delta.y = MathF.Min(delta.y, start.ResolvedRect.Height);
        vector2 size = start.SizeDelta;
        vector2 compensation = default;
        if (signX != 0.0f)
        {
            size.x = start.SizeDelta.x + signX * delta.x;
            compensation.x = (signX > 0.0f ? start.Pivot.x : 1.0f - start.Pivot.x) * delta.x;
        }
        if (signY != 0.0f)
        {
            size.y = start.SizeDelta.y + signY * delta.y;
            compensation.y = (signY > 0.0f ? start.Pivot.y : 1.0f - start.Pivot.y) * delta.y;
        }

        layout.SetSizeDelta(size);
        //按节点旋转与缩放补偿未拖动的边
        matrix4x4 matrix = start.LocalMatrix;
        vector3 translation = new(matrix[0] * compensation.x + matrix[4] * compensation.y,
            matrix[1] * compensation.x + matrix[5] * compensation.y,
            matrix[2] * compensation.x + matrix[6] * compensation.y);
        if (layout.Ens.GetComponent<Canvas>() is Canvas)
            layout.Ens.Transform.SetLocalPosition(new vector3(start.Position.x + translation.x,
                start.Position.y + translation.y, start.Position.z + translation.z));
        else
        {
            layout.SetOffset(new vector2(start.Offset.x + translation.x, start.Offset.y + translation.y));
            if (translation.z != 0)
                layout.Ens.Transform.SetLocalPosition(new vector3(start.Position.x, start.Position.y, start.Position.z + translation.z));
        }
    }

    //按画布模式将节点坐标映射到场景屏幕
    private static bool TryProjectLayoutPoint(UINode node, vector2 point, out vector2 screen)
    {
        screen = default;
        return TryGetSceneMatrix(node, out matrix4x4 matrix)
            && EditorSceneHandles.ProjectPoint(ToScenePoint(matrix, point), out screen);
    }

    /// <summary>获取布局节点在场景预览中的变换。</summary>
    public static bool TryGetSceneMatrix(UINode node, out matrix4x4 matrix)
    {
        matrix = default;
        UINode? root = node;
        while (root != null && root.Canvas == null) root = root.Parent;
        if (root?.Canvas == null) return false;
        Canvas canvas = root.Canvas;
        if (canvas.GetRenderMode() == CanvasRenderMode.Offscreen && node.Canvas == null) return false;
        matrix = canvas.GetRenderMode() == CanvasRenderMode.WorldSpace
            ? node.WorldMatrix : matrix4x4.Multiply(root.ScenePreviewMatrix, node.CanvasMatrix);
        return true;
    }

    //将布局点变换到场景空间
    private static vector3 ToScenePoint(in matrix4x4 matrix, vector2 point) => new(
        matrix[0] * point.x + matrix[4] * point.y + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[13],
        matrix[2] * point.x + matrix[6] * point.y + matrix[14]);

    private bool TryResolve(out UILayout? layout, out LayoutBaseline start)
    {
        start = baseline ?? default;
        layout = null;
        if (baseline == null) return false;
        return start.TryResolve(out layout) && layout != null;
    }

    //被驱动的轴只读：提示一次 owner，避免每帧刷屏。
    private void ReportDrivenAxis(UILayout layout)
    {
        if (reportedDrivenAxis) return;
        reportedDrivenAxis = true;
        EditorGUI.Label("This axis is driven by a control; edit it in the owning control instead.");
        _ = layout;
    }

    //检查控件或父布局组是否驱动当前矩形
    private static bool IsDriven(UILayout layout)
    {
        if (layout.HasDrivenRect || layout.HasDrivenOffset) return true;
        UINode? node = UIWorldContext.Current?.FindNode(layout.EnsId);
        return !layout.GetIgnoreLayout() && node?.Parent?.GetElement<UILayoutGroup>() != null;
    }

    //一次编辑的起始快照；只存稳定 ID 与字段值，不闭包保存可能失效的包装。
    private readonly struct LayoutBaseline
    {
        private readonly EnsId ens;
        private readonly vector2 anchorMin;
        private readonly vector2 anchorMax;
        private readonly vector2 pivot;
        private readonly vector2 offset;
        private readonly vector2 sizeDelta;
        private readonly UIRect resolved;
        private readonly vector3 position;
        private readonly matrix4x4 localMatrix;

        private LayoutBaseline(EnsId ens, UILayout layout, UIRect resolved)
        {
            this.ens = ens;
            anchorMin = layout.GetAnchorMin();
            anchorMax = layout.GetAnchorMax();
            pivot = layout.GetPivot();
            offset = layout.GetOffset();
            sizeDelta = layout.GetSizeDelta();
            this.resolved = resolved;
            Transform transform = layout.Ens.Transform;
            position = transform.GetLocalPosition();
            //节点自身的缩放就是把布局单位换算到场景单位的比例：画布根是画布刻度，子节点通常是 1。
            vector3 scale = transform.GetLocalScale();
            localMatrix = matrix4x4.Trs(default, transform.GetLocalRotation(), scale);
        }

        internal vector2 Offset => offset;
        internal vector2 SizeDelta => sizeDelta;
        internal vector2 Pivot => pivot;
        internal UIRect ResolvedRect => resolved;
        internal vector3 Position => position;
        internal matrix4x4 LocalMatrix => localMatrix;

        internal static LayoutBaseline Capture(UILayout layout) =>
            new(layout.EnsId, layout, layout.GetResolvedRect());

        internal bool TryResolve(out UILayout? layout)
        {
            layout = null;
            Ens owner = Ens.FromId(ens);
            if (!owner.IsValid) return false;
            layout = owner.GetComponent<UILayout>();
            return layout != null;
        }

        internal void Apply(UILayout layout)
        {
            layout.SetAnchors(anchorMin, anchorMax);
            layout.SetPivot(pivot);
            layout.SetOffset(offset);
            layout.SetSizeDelta(sizeDelta);
            layout.Ens.Transform.SetLocalPosition(position);
        }

        internal bool Matches(in LayoutBaseline other) =>
            anchorMin.x == other.anchorMin.x && anchorMin.y == other.anchorMin.y
            && anchorMax.x == other.anchorMax.x && anchorMax.y == other.anchorMax.y
            && pivot.x == other.pivot.x && pivot.y == other.pivot.y
            && offset.x == other.offset.x && offset.y == other.offset.y
            && sizeDelta.x == other.sizeDelta.x && sizeDelta.y == other.sizeDelta.y
            && position.x == other.position.x && position.y == other.position.y && position.z == other.position.z;
    }
}
