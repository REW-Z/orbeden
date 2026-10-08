using System;

namespace Orbeden;

/// <summary>
/// 滚动容器。内容必须是直接子节点，滚动通过驱动内容偏移实现；
/// 滚动条与偏移双向同步，内部同步不通知，一次变化只发一次 ScrollChanged。
/// 裁剪只包围内容子树，独立的滚动条不受影响。
/// </summary>
public class ScrollBox : UIControl
{
    /// <summary>惯性速度归零的阈值。</summary>
    public const float MinimumVelocity = 0.1f;

    [SerializeField] private UILayout? content;
    [SerializeField] private ScrollBar? horizontalBar;
    [SerializeField] private ScrollBar? verticalBar;
    [SerializeField] private bool horizontal;
    [SerializeField] private bool vertical = true;
    [SerializeField] private vector2 scrollOffset;
    [SerializeField] private float wheelStep = 40.0f;
    [SerializeField] private float deceleration = 10.0f;

    /// <summary>滚动偏移变化。派发时触发；同值不通知。</summary>
    public event Action<vector2>? ScrollChanged;

    //惯性速度；触摸释放时接手最近位移。
    private vector2 velocity;
    //触摸拖动中的上一个位置与时间戳。
    private vector2 touchLastPosition;
    private double touchLastTime;
    private uint dragPointerId = uint.MaxValue;
    //内部同步时抑制通知，避免 bar 与 offset 来回触发。
    private bool suppressNotify;

    /// <summary>创建滚动容器组件包装。</summary>
    public ScrollBox(Ens ens) : base(ens)
    {
    }

    /// <summary>内容布局。</summary>
    public UILayout? GetContent() => content;

    /// <summary>设置内容布局。</summary>
    public void SetContent(UILayout? value)
    {
        content = value;
        ApplyOffset();
    }

    /// <summary>横向滚动条。</summary>
    public ScrollBar? GetHorizontalBar() => horizontalBar;

    /// <summary>
    /// 设置横向滚动条：先退订旧引用再订阅新引用，避免旧条继续驱动本容器。
    /// </summary>
    public void SetHorizontalBar(ScrollBar? value)
    {
        if (ReferenceEquals(horizontalBar, value)) return;
        if (horizontalBar != null) horizontalBar.ValueChanged -= OnHorizontalBarChanged;
        horizontalBar = value;
        if (horizontalBar != null) horizontalBar.ValueChanged += OnHorizontalBarChanged;
        SynchronizeBars();
    }

    /// <summary>纵向滚动条。</summary>
    public ScrollBar? GetVerticalBar() => verticalBar;

    /// <summary>设置纵向滚动条：同样先退订再订阅。</summary>
    public void SetVerticalBar(ScrollBar? value)
    {
        if (ReferenceEquals(verticalBar, value)) return;
        if (verticalBar != null) verticalBar.ValueChanged -= OnVerticalBarChanged;
        verticalBar = value;
        if (verticalBar != null) verticalBar.ValueChanged += OnVerticalBarChanged;
        SynchronizeBars();
    }

    /// <summary>是否允许横向滚动。</summary>
    public bool GetHorizontal() => horizontal;

    /// <summary>设置是否允许横向滚动。</summary>
    public void SetHorizontal(bool value)
    {
        if (horizontal == value) return;
        horizontal = value;
        ClampOffset(true);
        SynchronizeBars();
    }

    /// <summary>是否允许纵向滚动。</summary>
    public bool GetVertical() => vertical;

    /// <summary>设置是否允许纵向滚动。</summary>
    public void SetVertical(bool value)
    {
        if (vertical == value) return;
        vertical = value;
        ClampOffset(true);
        SynchronizeBars();
    }

    /// <summary>当前滚动偏移。</summary>
    public vector2 GetScrollOffset() => scrollOffset;

    /// <summary>滚轮步长。</summary>
    public float GetWheelStep() => wheelStep;

    /// <summary>设置滚轮步长。</summary>
    public void SetWheelStep(float value)
    {
        if (!float.IsFinite(value) || value < 0.0f) return;
        wheelStep = value;
    }

    /// <summary>惯性减速度。</summary>
    public float GetDeceleration() => deceleration;

    /// <summary>设置惯性减速度；负值与非有限值拒绝写入。</summary>
    public void SetDeceleration(float value)
    {
        if (!float.IsFinite(value) || value < 0.0f || deceleration == value) return;
        deceleration = value;
    }

    /// <summary>设置滚动偏移；先夹紧到有效范围，再驱动内容与同步滚动条。</summary>
    public void SetScrollOffset(vector2 newOffset, bool notify = true)
    {
        vector2 clamped = ClampToRange(newOffset);
        if (clamped.x == scrollOffset.x && clamped.y == scrollOffset.y) return;

        scrollOffset = clamped;
        ApplyOffset();
        SynchronizeBars();
        if (notify && !suppressNotify) RaiseEvent(UIEventIds.ScrollChanged, UIEventPayload.Point(scrollOffset));
    }

    /// <summary>
    /// 推进惯性。到边界对应轴的速度归零；速度绝对值小于阈值也归零；不实现弹性越界。
    /// </summary>
    public void AdvanceInertia(float deltaTime)
    {
        if (!float.IsFinite(deltaTime) || deltaTime <= 0.0f) return;
        if (velocity.x == 0.0f && velocity.y == 0.0f) return;

        vector2 next = new(scrollOffset.x + velocity.x * deltaTime, scrollOffset.y + velocity.y * deltaTime);
        vector2 clamped = ClampToRange(next);

        //撞到边界就把那一轴的速度清零。
        if (clamped.x != next.x) velocity.x = 0.0f;
        if (clamped.y != next.y) velocity.y = 0.0f;

        float decay = MathF.Exp(-deceleration * deltaTime);
        velocity = new vector2(velocity.x * decay, velocity.y * decay);
        if (MathF.Abs(velocity.x) < MinimumVelocity) velocity.x = 0.0f;
        if (MathF.Abs(velocity.y) < MinimumVelocity) velocity.y = 0.0f;

        SetScrollOffset(clamped);
    }

    /// <summary>滚轮：按步长滚动；本容器在该方向已经到边界就交给外层。</summary>
    public override bool OnScroll(vector2 delta)
    {
        if (!CanInteract()) return false;

        //驱动偏移与内容偏移方向相反：内容向下走，偏移的 y 变小。
        vector2 wanted = new(scrollOffset.x - delta.x * wheelStep, scrollOffset.y + delta.y * wheelStep);
        vector2 clamped = ClampToRange(wanted);

        bool handled = false;
        if (horizontal && clamped.x != scrollOffset.x) handled = true;
        if (vertical && clamped.y != scrollOffset.y) handled = true;
        //已经到边界：这一轴交给外层，但另一轴可能还能动。
        if (!handled) return false;

        velocity = default;
        SetScrollOffset(clamped);
        return true;
    }

    /// <summary>指针按下：内容上按下即开始拖动（触摸惯性要靠它）。</summary>
    public override void OnPointerDown(in UIPointerEvent input)
    {
        //只让触摸接手惯性；鼠标拖动留给内容里的控件。
        if (input.pointerId == 0) return;
        dragPointerId = input.pointerId;
        touchLastPosition = input.position;
        touchLastTime = input.timestamp;
        velocity = default;
    }

    /// <summary>指针移动：拖动内容并记录速度。</summary>
    public override void OnPointerMove(in UIPointerEvent input)
    {
        if (dragPointerId != input.pointerId) return;

        vector2 delta = new(input.position.x - touchLastPosition.x, input.position.y - touchLastPosition.y);
        double elapsed = input.timestamp - touchLastTime;
        touchLastPosition = input.position;
        touchLastTime = input.timestamp;

        //内容跟着手指走：偏移与手指位移相反。
        SetScrollOffset(new vector2(scrollOffset.x - delta.x, scrollOffset.y + delta.y));

        //记录速度供释放后的惯性使用。
        if (elapsed > 1e-4) velocity = new vector2(-delta.x / (float)elapsed, delta.y / (float)elapsed);
    }

    /// <summary>指针抬起：保留最近速度，交给惯性推进。</summary>
    public override void OnPointerUp(in UIPointerEvent input)
    {
        if (dragPointerId != input.pointerId) return;
        dragPointerId = uint.MaxValue;
    }

    /// <summary>指针取消：同样结束拖动。</summary>
    public override void OnPointerCancel(in UIPointerEvent input)
    {
        if (dragPointerId != input.pointerId) return;
        dragPointerId = uint.MaxValue;
        velocity = default;
    }

    /// <summary>内容或视口变化后调用：先夹紧偏移，再同步滚动条。</summary>
    public void SynchronizeBars()
    {
        ClampOffset(false);
        ApplyPageSizes();
        ApplyBarValues();
    }

    /// <summary>代码事件在派发时触发。</summary>
    protected override void RaiseCodeEvent(int eventId, in UIEventPayload payload)
    {
        if (eventId == UIEventIds.ScrollChanged) ScrollChanged?.Invoke(payload.point);
    }

    /// <summary>控件摘除：退订滚动条并撤销内容上的平移覆盖。</summary>
    protected override void OnUIDetached()
    {
        if (horizontalBar != null) horizontalBar.ValueChanged -= OnHorizontalBarChanged;
        if (verticalBar != null) verticalBar.ValueChanged -= OnVerticalBarChanged;
        content?.ClearDrivenOffset(this);
    }

    /// <summary>每帧推进惯性；由上下文在渲染准备阶段调用。</summary>
    internal void Tick(float deltaTime) => AdvanceInertia(deltaTime);

    //滚动条变化 → 偏移；内部写入不通知。
    private void OnHorizontalBarChanged(float barValue)
    {
        if (horizontalBar == null) return;
        suppressNotify = true;
        SetScrollOffset(new vector2(-BarToOffset(barValue, true), scrollOffset.y), notify: false);
        suppressNotify = false;
    }

    private void OnVerticalBarChanged(float barValue)
    {
        if (verticalBar == null) return;
        suppressNotify = true;
        SetScrollOffset(new vector2(scrollOffset.x, -BarToOffset(barValue, false)), notify: false);
        suppressNotify = false;
    }

    //偏移 → 滚动条；内部同步不通知。
    private void ApplyBarValues()
    {
        suppressNotify = true;
        if (horizontalBar != null)
        {
            horizontalBar.SetValue(OffsetToBar(scrollOffset.x, true), notify: false);
            horizontalBar.SetPageSize(PageSize(true), notify: false);
        }
        if (verticalBar != null)
        {
            verticalBar.SetValue(OffsetToBar(scrollOffset.y, false), notify: false);
            verticalBar.SetPageSize(PageSize(false), notify: false);
        }
        suppressNotify = false;
    }

    //可见比例：viewport / content，内容为空时取 1。
    private float PageSize(bool horizontalAxis)
    {
        UIRect viewport = GetLayout()?.GetResolvedRect() ?? default;
        UIRect inner = content?.GetResolvedRect() ?? default;
        float view = horizontalAxis ? viewport.Width : viewport.Height;
        float total = horizontalAxis ? inner.Width : inner.Height;
        if (total <= 0.0f) return 1.0f;
        return Math.Clamp(view / total, 0.0f, 1.0f);
    }

    private void ApplyPageSizes()
    {
        if (horizontalBar != null) horizontalBar.SetPageSize(PageSize(true), notify: false);
        if (verticalBar != null) verticalBar.SetPageSize(PageSize(false), notify: false);
    }

    //偏移到滚动条值：0 表示起点（内容左上），向内容末端推进。
    private float OffsetToBar(float offset, bool horizontalAxis)
    {
        float range = Range(horizontalAxis);
        if (range <= 0.0f) return 0.0f;
        float normalized = horizontalAxis ? -offset / range : -offset / range;
        return Math.Clamp(normalized, 0.0f, 1.0f);
    }

    private float BarToOffset(float barValue, bool horizontalAxis) => barValue * Range(horizontalAxis);

    //可滚动范围：内容尺寸减视口尺寸，负值取零；关闭的轴固定为零。
    private float Range(bool horizontalAxis)
    {
        if (horizontalAxis && !horizontal) return 0.0f;
        if (!horizontalAxis && !vertical) return 0.0f;

        UIRect viewport = GetLayout()?.GetResolvedRect() ?? default;
        UIRect inner = content?.GetResolvedRect() ?? default;
        float view = horizontalAxis ? viewport.Width : viewport.Height;
        float total = horizontalAxis ? inner.Width : inner.Height;
        return MathF.Max(total - view, 0.0f);
    }

    private vector2 ClampToRange(vector2 value)
    {
        float rangeX = Range(true);
        float rangeY = Range(false);
        return new vector2(Math.Clamp(value.x, -rangeX, 0.0f), Math.Clamp(value.y, -rangeY, 0.0f));
    }

    private void ClampOffset(bool notify)
    {
        vector2 clamped = ClampToRange(scrollOffset);
        if (clamped.x == scrollOffset.x && clamped.y == scrollOffset.y) return;
        scrollOffset = clamped;
        ApplyOffset();
        if (notify && !suppressNotify) RaiseEvent(UIEventIds.ScrollChanged, UIEventPayload.Point(scrollOffset));
    }

    //驱动内容平移：在常规解析之上叠加，因此可以反复调用而不会累积。
    private void ApplyOffset()
    {
        if (content == null) return;
        content.SetDrivenOffset(scrollOffset, this);
    }

    /// <summary>配置错误描述；非空时按空容器处理，检视面板据此报错。</summary>
    public string GetDiagnostic()
    {
        if (content == null) return "ScrollBox has no content; treated as an empty container.";
        UIWorldContext? context = UIWorldContext.Current;
        UINode? self = context?.FindNode(EnsId);
        UINode? inner = context?.FindNode(content.EnsId);
        if (self == null || inner == null) return string.Empty;
        //内容必须是直接子节点：间接子树无法用一次平移驱动。
        if (!ReferenceEquals(inner.Parent, self)) return "ScrollBox content must be a direct child of the box.";
        return string.Empty;
    }
}
