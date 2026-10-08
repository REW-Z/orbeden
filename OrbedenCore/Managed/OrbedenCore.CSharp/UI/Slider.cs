using System;

namespace Orbeden;

/// <summary>滑动方向。</summary>
public enum UIOrientation : uint
{
    /// <summary>横向：左到右是增大的方向。</summary>
    Horizontal = 0,

    /// <summary>纵向：下到上是增大的方向。</summary>
    Vertical = 1,
}

/// <summary>
/// 滑动条。轨道、拇指与填充各自是独立的 UILayout 子节点；
/// 控件只驱动它们的运行期矩形，不写回这些子节点的锚点与尺寸配置。
/// </summary>
public class Slider : UIControl
{
    [SerializeField] private float minimum;
    [SerializeField] private float maximum = 1.0f;
    [SerializeField] private float value;
    [SerializeField] private bool wholeNumbers;
    [SerializeField] private UIOrientation orientation = UIOrientation.Horizontal;
    [SerializeField] private bool reverse;
    [SerializeField] private UILayout? track;
    [SerializeField] private UILayout? thumb;
    [SerializeField] private UILayout? fill;

    /// <summary>值变化。派发时触发；同值不通知。</summary>
    public event Action<float>? ValueChanged;

    private uint dragPointerId = uint.MaxValue;

    /// <summary>创建滑动条组件包装。</summary>
    public Slider(Ens ens) : base(ens)
    {
    }

    /// <summary>下界。</summary>
    public float GetMinimum() => minimum;

    /// <summary>上界。</summary>
    public float GetMaximum() => maximum;

    /// <summary>当前值。</summary>
    public float GetValue() => value;

    /// <summary>是否按整数取值。</summary>
    public bool GetWholeNumbers() => wholeNumbers;

    /// <summary>滑动方向。</summary>
    public UIOrientation GetOrientation() => orientation;

    /// <summary>是否反向。</summary>
    public bool GetReverse() => reverse;

    /// <summary>轨道布局；为空时保留值但禁用指针拖动。</summary>
    public UILayout? GetTrack() => track;

    /// <summary>设置轨道布局。</summary>
    public void SetTrack(UILayout? value) => track = value;

    /// <summary>拇指布局；为空时保留值但禁用指针拖动。</summary>
    public UILayout? GetThumb() => thumb;

    /// <summary>设置拇指布局。</summary>
    public void SetThumb(UILayout? value) => thumb = value;

    /// <summary>填充布局；为空时不绘制填充。</summary>
    public UILayout? GetFill() => fill;

    /// <summary>设置填充布局。</summary>
    public void SetFill(UILayout? value) => fill = value;

    /// <summary>设置滑动方向。</summary>
    public void SetOrientation(UIOrientation value)
    {
        if (orientation == value) return;
        orientation = value;
        DriveVisuals();
    }

    /// <summary>设置是否反向。</summary>
    public void SetReverse(bool value)
    {
        if (reverse == value) return;
        reverse = value;
        DriveVisuals();
    }

    /// <summary>设置是否按整数取值；开启后立即把当前值对齐到整数。</summary>
    public void SetWholeNumbers(bool value)
    {
        if (wholeNumbers == value) return;
        wholeNumbers = value;
        SetValue(value ? UIRangeMath.RoundToWhole(this.value) : this.value);
    }

    /// <summary>原子设置两界；下界大于上界拒绝，零区间把值固定为下界。</summary>
    public void SetRange(float newMinimum, float newMaximum)
    {
        if (!float.IsFinite(newMinimum) || !float.IsFinite(newMaximum) || newMinimum > newMaximum) return;
        minimum = newMinimum;
        maximum = newMaximum;

        float clamped = UIRangeMath.Clamp(value, minimum, maximum);
        if (wholeNumbers) clamped = UIRangeMath.RoundToWhole(clamped);
        clamped = UIRangeMath.Clamp(clamped, minimum, maximum);
        if (clamped != value)
        {
            value = clamped;
            RaiseEvent(UIEventIds.ValueChanged, UIEventPayload.Number(value));
        }
        DriveVisuals();
    }

    /// <summary>
    /// 设置值。先夹紧到区间、整数模式舍入，再排事件；值没变就不通知。
    /// </summary>
    public void SetValue(float newValue, bool notify = true)
    {
        float clamped = UIRangeMath.Clamp(newValue, minimum, maximum);
        if (wholeNumbers) clamped = UIRangeMath.RoundToWhole(clamped);
        clamped = UIRangeMath.Clamp(clamped, minimum, maximum);
        if (clamped == value) return;

        value = clamped;
        DriveVisuals();
        if (notify) RaiseEvent(UIEventIds.ValueChanged, UIEventPayload.Number(value));
    }

    /// <summary>指针按下：落在轨道上直接跳转，随后交给捕获拖动。</summary>
    public override void OnPointerDown(in UIPointerEvent input)
    {
        if (!CanInteract() || !TryBeginDrag(input.pointerId)) return;
        JumpToPointer(input);
    }

    /// <summary>指针移动：拖动中跟随指针。</summary>
    public override void OnPointerMove(in UIPointerEvent input)
    {
        if (dragPointerId != input.pointerId) return;
        JumpToPointer(input);
    }

    /// <summary>指针抬起或取消：结束拖动。</summary>
    public override void OnPointerUp(in UIPointerEvent input) => EndDrag(input.pointerId);

    /// <summary>指针取消：结束拖动。</summary>
    public override void OnPointerCancel(in UIPointerEvent input) => EndDrag(input.pointerId);

    /// <summary>方向导航：步长为整数模式的 1，否则是区间长度的十分之一。</summary>
    public override bool OnNavigate(UINavigation direction)
    {
        bool forward = orientation == UIOrientation.Horizontal
            ? direction == UINavigation.Right
            : direction == UINavigation.Up;
        bool backward = orientation == UIOrientation.Horizontal
            ? direction == UINavigation.Left
            : direction == UINavigation.Down;
        if (!forward && !backward) return false;

        float span = maximum - minimum;
        float step = wholeNumbers ? 1.0f : span * 0.1f;
        SetValue(value + (forward ? step : -step));
        return true;
    }

    /// <summary>代码事件在派发时触发。</summary>
    protected override void RaiseCodeEvent(int eventId, in UIEventPayload payload)
    {
        if (eventId == UIEventIds.ValueChanged) ValueChanged?.Invoke(payload.value);
    }

    /// <summary>控件摘除：撤销驱动过的矩形。</summary>
    protected override void OnUIDetached()
    {
        thumb?.ClearDrivenRect(this);
        fill?.ClearDrivenRect(this);
    }

    //开始拖动：每个控件同一时刻只接受一个指针。
    private bool TryBeginDrag(uint pointerId)
    {
        if (dragPointerId != uint.MaxValue) return false;
        //缺轨道或拇指时保留值，但不允许指针拖动。
        if (track == null || thumb == null) return false;
        dragPointerId = pointerId;
        return true;
    }

    private void EndDrag(uint pointerId)
    {
        if (dragPointerId == pointerId) dragPointerId = uint.MaxValue;
    }

    //按指针位置直接取值：点击轨道即跳转。
    private void JumpToPointer(in UIPointerEvent input)
    {
        if (track == null) return;
        UINode? node = UIWorldContext.Current?.FindNode(track.EnsId);
        if (node == null) return;

        UIRect trackRect = track.GetResolvedRect();
        if (trackRect.Width <= 0.0f || trackRect.Height <= 0.0f) return;

        //指针换算到轨道所在节点的局部空间。
        if (!TryToTrackLocal(node, input.position, out vector2 local)) return;
        float normalized = orientation == UIOrientation.Horizontal
            ? (local.x - trackRect.min.x) / trackRect.Width
            : (local.y - trackRect.min.y) / trackRect.Height;
        normalized = Math.Clamp(normalized, 0.0f, 1.0f);
        if (reverse) normalized = 1.0f - normalized;
        SetValue(UIRangeMath.Denormalize(normalized, minimum, maximum));
    }

    //驱动拇指与填充的运行期矩形，不修改它们的配置。
    private void DriveVisuals()
    {
        float normalized = UIRangeMath.Normalize(value, minimum, maximum);
        float direction = reverse ? 1.0f - normalized : normalized;
        DriveThumb(direction);
        DriveFill(direction);
    }

    private void DriveThumb(float normalized)
    {
        if (track == null || thumb == null) return;

        //轨道与拇指换算到共同空间（它们的父级空间）；两者的锚点就是各自原点的落点。
        UIRect trackRect = InParentSpace(track);
        UIRect thumbRect = InParentSpace(thumb);
        if (orientation == UIOrientation.Horizontal)
        {
            //拇指中心在扣除自身长度后的轨道内移动；有效长度不大于零时停在起点。
            float travel = trackRect.Width - thumbRect.Width;
            float centerX = travel > 0.0f ? trackRect.min.x + thumbRect.Width * 0.5f + travel * normalized : trackRect.min.x;
            float centerY = trackRect.min.y + trackRect.Height * 0.5f;
            UIRect driven = new(new vector2(centerX - thumbRect.Width * 0.5f, centerY - thumbRect.Height * 0.5f), thumbRect.size);
            thumb.SetDrivenRect(driven, this);
            return;
        }

        float verticalTravel = trackRect.Height - thumbRect.Height;
        float centerXv = trackRect.min.x + trackRect.Width * 0.5f;
        float centerYv = verticalTravel > 0.0f
            ? trackRect.min.y + thumbRect.Height * 0.5f + verticalTravel * normalized
            : trackRect.min.y;
        UIRect drivenV = new(new vector2(centerXv - thumbRect.Width * 0.5f, centerYv - thumbRect.Height * 0.5f), thumbRect.size);
        thumb.SetDrivenRect(drivenV, this);
    }

    private void DriveFill(float normalized)
    {
        if (track == null || fill == null) return;

        UIRect trackRect = InParentSpace(track);
        UIRect fillRect = InParentSpace(fill);
        UIRect driven = orientation == UIOrientation.Horizontal
            ? new UIRect(trackRect.min, new vector2(trackRect.Width * normalized, fillRect.Height))
            : new UIRect(trackRect.min, new vector2(fillRect.Width, trackRect.Height * normalized));
        fill.SetDrivenRect(driven, this);
    }

    //把子布局的解析矩形换算到它的父级空间：解析矩形在自身局部空间，原点落在锚点上。
    private static UIRect InParentSpace(UILayout layout)
    {
        UIRect local = layout.GetResolvedRect();
        vector2 anchor = layout.GetResolvedAnchorPoint();
        return new UIRect(new vector2(local.min.x + anchor.x, local.min.y + anchor.y), local.size);
    }

    //窗口逻辑点换算到轨道节点局部空间。
    private bool TryToTrackLocal(UINode node, vector2 windowPoint, out vector2 local)
    {
        local = default;
        return UICanvasSpace.TryGetCanvasView(node, out UIView view)
            && UICanvasSpace.TryWindowToLocal(node, view, windowPoint, out local);
    }
}
