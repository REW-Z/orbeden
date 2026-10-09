using System;

namespace Orbeden;

/// <summary>
/// 滚动条。值域固定为 [0,1]：value 是滚动位置，pageSize 是可见比例。
/// 拇指长度按 pageSize 缩放；点击拇指外侧按一页移动，拖动按剩余行程换算。
/// </summary>
public class ScrollBar : UIControl
{
    [SerializeField] private float value;
    [SerializeField] private float pageSize = 1.0f;
    [SerializeField] private UIOrientation orientation = UIOrientation.Horizontal;
    [SerializeField] private bool reverse;
    [SerializeField] private UILayout? track;
    [SerializeField] private UILayout? thumb;

    /// <summary>值变化。派发时触发；同值不通知。</summary>
    public OrbEvent<float> ValueChanged = new();

    private uint dragPointerId = uint.MaxValue;
    //拖动起点：按下时的指针位置、值，以及当时的可用行程。
    private float dragStartValue;
    private vector2 dragStartPosition;
    private float dragTravel;

    /// <summary>创建滚动条组件包装。</summary>
    public ScrollBar(Ens ens) : base(ens)
    {
    }

    /// <summary>滚动位置。</summary>
    public float GetValue() => value;

    /// <summary>可见比例。</summary>
    public float GetPageSize() => pageSize;

    /// <summary>滚动方向。</summary>
    public UIOrientation GetOrientation() => orientation;

    /// <summary>是否反向。</summary>
    public bool GetReverse() => reverse;

    /// <summary>轨道布局；为空时保留值但禁用指针拖动。</summary>
    public UILayout? GetTrack() => track;

    /// <summary>设置轨道布局。</summary>
    public void SetTrack(UILayout? value)
    {
        track = value;
        DriveThumb();
    }

    /// <summary>拇指布局；为空时保留值但禁用指针拖动。</summary>
    public UILayout? GetThumb() => thumb;

    /// <summary>设置拇指布局。</summary>
    public void SetThumb(UILayout? value)
    {
        thumb = value;
        DriveThumb();
    }

    /// <summary>设置滚动方向。</summary>
    public void SetOrientation(UIOrientation newOrientation)
    {
        if (orientation == newOrientation) return;
        orientation = newOrientation;
        DriveThumb();
    }

    /// <summary>设置是否反向。</summary>
    public void SetReverse(bool newReverse)
    {
        if (reverse == newReverse) return;
        reverse = newReverse;
        DriveThumb();
    }

    /// <summary>设置可见比例；限于 [0,1]，为 1 时滚动位置固定为 0。</summary>
    public void SetPageSize(float newPageSize, bool notify = true)
    {
        float clamped = float.IsFinite(newPageSize) ? Math.Clamp(newPageSize, 0.0f, 1.0f) : 1.0f;
        bool changed = clamped != pageSize;
        pageSize = clamped;

        //整页可见时没有可滚动的内容，位置归零。
        if (pageSize >= 1.0f && value != 0.0f)
        {
            value = 0.0f;
            changed = true;
        }
        DriveThumb();
        if (changed && notify) RaiseEvent(UIEventIds.ValueChanged, UIEventPayload.Number(value));
    }

    /// <summary>设置滚动位置；限于 [0,1]，同值不通知。</summary>
    public void SetValue(float newValue, bool notify = true)
    {
        float clamped = float.IsFinite(newValue) ? Math.Clamp(newValue, 0.0f, 1.0f) : 0.0f;
        //整页可见时位置保持 0。
        if (pageSize >= 1.0f) clamped = 0.0f;
        if (clamped == value) return;

        value = clamped;
        DriveThumb();
        if (notify) RaiseEvent(UIEventIds.ValueChanged, UIEventPayload.Number(value));
    }

    /// <summary>指针按下：落在拇指上开始拖动，落在拇指外侧按一页跳转。</summary>
    public override void OnPointerDown(in UIPointerEvent input)
    {
        if (!CanInteract() || track == null || thumb == null) return;
        UINode? node = UIWorldContext.Current?.FindNode(track.EnsId);
        if (node == null) return;
        if (!TryToTrackLocal(node, input.position, out vector2 local)) return;

        if (ContainsThumb(local))
        {
            //拖动起点记在按下这一刻，拖动按位移换算而不是按绝对位置。
            dragPointerId = input.pointerId;
            dragStartValue = value;
            dragStartPosition = local;
            dragTravel = TrackLength() - ThumbLength();
            return;
        }

        //点击拇指外侧：按一页移动。
        float direction = local.x >= 0.0f ? 1.0f : -1.0f;
        if (orientation == UIOrientation.Vertical) direction = local.y >= 0.0f ? 1.0f : -1.0f;
        if (reverse) direction = -direction;
        SetValue(value + direction * pageSize);
    }

    /// <summary>指针移动：拖动中按位移换算值。</summary>
    public override void OnPointerMove(in UIPointerEvent input)
    {
        if (dragPointerId != input.pointerId) return;
        UINode? node = track == null ? null : UIWorldContext.Current?.FindNode(track.EnsId);
        if (node == null) return;
        if (!TryToTrackLocal(node, input.position, out vector2 local)) return;

        //有效行程不大于零时拖动不改变值。
        if (dragTravel <= 0.0f) return;
        float delta = orientation == UIOrientation.Horizontal
            ? local.x - dragStartPosition.x
            : local.y - dragStartPosition.y;
        if (reverse) delta = -delta;
        SetValue(dragStartValue + delta / dragTravel);
    }

    /// <summary>指针抬起：结束拖动。</summary>
    public override void OnPointerUp(in UIPointerEvent input) => EndDrag(input.pointerId);

    /// <summary>指针取消：结束拖动。</summary>
    public override void OnPointerCancel(in UIPointerEvent input) => EndDrag(input.pointerId);

    /// <summary>方向导航：按一页移动。</summary>
    public override bool OnNavigate(UINavigation direction)
    {
        bool forward = orientation == UIOrientation.Horizontal
            ? direction == UINavigation.Right
            : direction == UINavigation.Up;
        bool backward = orientation == UIOrientation.Horizontal
            ? direction == UINavigation.Left
            : direction == UINavigation.Down;
        if (!forward && !backward) return false;

        SetValue(value + (forward ? pageSize : -pageSize));
        return true;
    }

    /// <summary>代码事件在派发时触发。</summary>
    protected override void DispatchEvent(int eventId, in UIEventPayload payload)
    {
        if (eventId == UIEventIds.ValueChanged) ValueChanged.Dispatch(payload.value);
    }

    /// <summary>控件摘除：撤销驱动过的拇指矩形。</summary>
    protected override void OnUIDetached() => thumb?.ClearDrivenRect(this);

    private void EndDrag(uint pointerId)
    {
        if (dragPointerId == pointerId) dragPointerId = uint.MaxValue;
    }

    //指针是否落在拇指上；拇指还没解析过时按落在轨道起点处理。
    private bool ContainsThumb(vector2 local)
    {
        if (thumb == null) return false;
        UIRect thumbRect = thumb.GetResolvedRect();
        return thumbRect.Contains(local);
    }

    private float TrackLength()
    {
        if (track == null) return 0.0f;
        UIRect rect = track.GetResolvedRect();
        return orientation == UIOrientation.Horizontal ? rect.Width : rect.Height;
    }

    private float ThumbLength()
    {
        if (thumb == null) return 0.0f;
        UIRect rect = thumb.GetResolvedRect();
        return orientation == UIOrientation.Horizontal ? rect.Width : rect.Height;
    }

    //驱动拇指：长度按 pageSize 缩放，位置在扣除自身长度后的行程里移动。
    private void DriveThumb()
    {
        if (track == null || thumb == null) return;

        //换算到共同空间（两者的父级空间）后再算拇指落点。
        UIRect trackRect = InParentSpace(track);
        UIRect thumbRect = InParentSpace(thumb);
        float position = reverse ? 1.0f - value : value;

        if (orientation == UIOrientation.Horizontal)
        {
            float thumbWidth = trackRect.Width * pageSize;
            float travel = trackRect.Width - thumbWidth;
            float minX = trackRect.min.x + (travel > 0.0f ? travel * position : 0.0f);
            UIRect driven = new(new vector2(minX, trackRect.min.y), new vector2(thumbWidth, trackRect.Height));
            thumb.SetDrivenRect(driven, this);
            return;
        }

        float thumbHeight = trackRect.Height * pageSize;
        float verticalTravel = trackRect.Height - thumbHeight;
        float minY = trackRect.min.y + (verticalTravel > 0.0f ? verticalTravel * position : 0.0f);
        UIRect drivenV = new(new vector2(trackRect.min.x, minY), new vector2(trackRect.Width, thumbHeight));
        thumb.SetDrivenRect(drivenV, this);
    }

    //把子布局的解析矩形换算到它的父级空间。
    private static UIRect InParentSpace(UILayout layout)
    {
        UIRect local = layout.GetResolvedRect();
        vector2 anchor = layout.GetResolvedAnchorPoint();
        return new UIRect(new vector2(local.min.x + anchor.x, local.min.y + anchor.y), local.size);
    }

    private bool TryToTrackLocal(UINode node, vector2 windowPoint, out vector2 local)
    {
        local = default;
        return UICanvasSpace.TryGetCanvasView(node, out UIView view)
            && UICanvasSpace.TryWindowToLocal(node, view, windowPoint, out local);
    }
}
