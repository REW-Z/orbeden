using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 控件基类。交互状态（悬停、按下、焦点）由 UIInputRouter 统一维护，
/// 具体控件只重写自己关心的回调；状态色乘进提交时的 tint，不改目标图形的持久化颜色。
/// </summary>
public class UIControl : UIElement, IUIEventSource,
    IUIPointerDownHandler, IUIPointerUpHandler, IUIPointerMoveHandler,
    IUIPointerEnterHandler, IUIPointerExitHandler, IUIPointerCancelHandler,
    IUIScrollHandler, IUINavigationHandler, IUISubmitHandler, IUICancelHandler, IUIFocusHandler
{
    [SerializeField] private bool interactable = true;
    [SerializeField] private UIControl? navigationUp;
    [SerializeField] private UIControl? navigationDown;
    [SerializeField] private UIControl? navigationLeft;
    [SerializeField] private UIControl? navigationRight;

    [SerializeField] private color normalColor = new(1.0f, 1.0f, 1.0f, 1.0f);
    [SerializeField] private color hoverColor = new(0.9f, 0.9f, 0.9f, 1.0f);
    [SerializeField] private color pressedColor = new(0.7f, 0.7f, 0.7f, 1.0f);
    [SerializeField] private color disabledColor = new(0.5f, 0.5f, 0.5f, 0.5f);

    //由路由器维护的运行时状态，不持久化。
    private bool hovered;
    private bool pressed;
    private bool focused;
    private UIMeshBuilder? overlayMesh;
    private ulong overlayRevision = 1;
    private bool overlayDirty = true;

    /// <summary>创建控件包装。</summary>
    public UIControl(Ens ens) : base(ens)
    {
    }

    /// <summary>
    /// 触发一条控件事件：入队之后由派发器按 FIFO 处理，
    /// 回调里再触发的事件排到队尾，不会递归。
    /// </summary>
    protected void RaiseEvent(int eventId, in UIEventPayload payload)
    {
        UIWorldContext.Current?.EventDispatcher.Raise(this, eventId, payload, EnsId);
    }

    /// <summary>事件派发到本控件时的第一步：更新状态与视觉，再走绑定。</summary>
    internal void OnEvent(in UIQueuedEvent entry)
    {
        OnEventRaised(entry.eventId, entry.payload);

        DispatchEvent(entry.eventId, entry.payload);
    }

    /// <summary>派发控件的 OrbEvent，持久化调用先于运行时订阅执行。</summary>
    protected virtual void DispatchEvent(int eventId, in UIEventPayload payload)
    {
    }

    /// <summary>事件在队列里被丢弃时调用；派生控件据此撤销"已触发"之类的一次性状态。</summary>
    internal void OnEventDiscarded(in UIQueuedEvent entry)
    {
        OnEventDropped(entry.eventId);
    }

    //接口实现：派发器只通过这些入口触达控件，显式实现避免占用公开成员名。
    void IUIEventSource.OnEvent(in UIQueuedEvent entry) => OnEvent(entry);

    void IUIEventSource.OnEventDiscarded(in UIQueuedEvent entry) => OnEventDiscarded(entry);

    /// <summary>控件自己的事件处理：更新内部状态与视觉。默认无操作。</summary>
    protected virtual void OnEventRaised(int eventId, in UIEventPayload payload)
    {
    }

    /// <summary>事件被丢弃时的收尾。默认无操作。</summary>
    protected virtual void OnEventDropped(int eventId)
    {
    }

    /// <summary>是否可以交互；不可交互时不接收指针与导航。</summary>
    public bool IsInteractable() => interactable;

    /// <summary>设置是否可交互；关掉时取消悬停与按下状态。</summary>
    public void SetInteractable(bool value)
    {
        if (interactable == value) return;
        interactable = value;
        if (!interactable)
        {
            hovered = false;
            pressed = false;
        }
        RefreshVisualState();
    }

    /// <summary>取某个方向的显式导航目标；没有时为空。</summary>
    public UIControl? GetNavigation(UINavigation direction) => direction switch
    {
        UINavigation.Up => navigationUp,
        UINavigation.Down => navigationDown,
        UINavigation.Left => navigationLeft,
        _ => navigationRight,
    };

    /// <summary>设置某个方向的显式导航目标。</summary>
    public void SetNavigation(UINavigation direction, UIControl? value)
    {
        switch (direction)
        {
        case UINavigation.Up: navigationUp = value; break;
        case UINavigation.Down: navigationDown = value; break;
        case UINavigation.Left: navigationLeft = value; break;
        default: navigationRight = value; break;
        }
    }

    /// <summary>常态颜色。</summary>
    public color GetNormalColor() => normalColor;

    /// <summary>悬停颜色。</summary>
    public color GetHoverColor() => hoverColor;

    /// <summary>按下颜色。</summary>
    public color GetPressedColor() => pressedColor;

    /// <summary>禁用颜色。</summary>
    public color GetDisabledColor() => disabledColor;

    /// <summary>设置一组状态色。</summary>
    public void SetStateColors(color normal, color hover, color pressed, color disabled)
    {
        normalColor = normal;
        hoverColor = hover;
        pressedColor = pressed;
        disabledColor = disabled;
        RefreshVisualState();
    }

    /// <summary>当前是否被指针悬停。</summary>
    public bool IsHovered() => hovered;

    /// <summary>当前是否被按下。</summary>
    public bool IsPressed() => pressed;

    /// <summary>当前是否持有焦点。</summary>
    public bool HasFocus() => focused;

    /// <summary>请求焦点；只接受可交互且处于活动状态的控件。</summary>
    public void Focus()
    {
        if (!CanInteract()) return;
        UIWorldContext.Current?.InputRouter.SetFocus(this);
    }

    /// <summary>控件是否可用于交互：可交互、活动、且所属节点没有配置错误。</summary>
    public bool CanInteract() => interactable && IsUIActive();

    /// <summary>指针进入。</summary>
    public virtual void OnPointerEnter(in UIPointerEvent input)
    {
    }

    /// <summary>指针离开。</summary>
    public virtual void OnPointerExit(in UIPointerEvent input)
    {
    }

    /// <summary>指针按下。</summary>
    public virtual void OnPointerDown(in UIPointerEvent input)
    {
    }

    /// <summary>指针移动。</summary>
    public virtual void OnPointerMove(in UIPointerEvent input)
    {
    }

    /// <summary>指针抬起。</summary>
    public virtual void OnPointerUp(in UIPointerEvent input)
    {
    }

    /// <summary>指针取消。</summary>
    public virtual void OnPointerCancel(in UIPointerEvent input)
    {
    }

    /// <summary>滚轮；返回真表示已经处理，不再向祖先传播。</summary>
    public virtual bool OnScroll(vector2 delta) => false;

    /// <summary>方向导航；返回真表示已经处理，焦点不再移动。</summary>
    public virtual bool OnNavigate(UINavigation direction) => false;

    /// <summary>提交。</summary>
    public virtual void OnSubmit()
    {
    }

    /// <summary>取消。</summary>
    public virtual void OnCancel()
    {
    }

    /// <summary>焦点变化。</summary>
    public virtual void OnFocusChanged(bool value)
    {
    }

    //由路由器写入交互状态；状态色必须跟着变。
    internal bool Hovered
    {
        get => hovered;
        set
        {
            if (hovered == value) return;
            hovered = value;
            RefreshVisualState();
        }
    }

    internal bool Pressed
    {
        get => pressed;
        set
        {
            if (pressed == value) return;
            pressed = value;
            RefreshVisualState();
        }
    }

    internal bool Focused
    {
        get => focused;
        set
        {
            if (focused == value) return;
            focused = value;
            //离焦清按下状态：焦点走了以后不该再补一次点击。
            if (!focused) Pressed = false;
            OnFocusChanged(value);
            RefreshVisualState();
        }
    }

    /// <summary>附加网格内容版本。</summary>
    internal ulong OverlayRevision => overlayRevision;

    /// <summary>获取所有摄像机共用的附加网格。</summary>
    public UIMeshBuilder? GetOverlay()
    {
        if (!HasOverlay) return null;
        overlayMesh ??= new UIMeshBuilder();
        float scale = UIWorldContext.Current?.GetRasterScale(GetCanvas()) ?? 1.0f;
        bool invalidated = IsOverlayInvalidated();
        if (!overlayDirty && overlayMesh.ViewScale == scale && !invalidated) return overlayMesh;

        //重建附加网格
        overlayDirty = false;
        overlayMesh.Clear();
        overlayMesh.ViewScale = scale;
        try
        {
            PopulateOverlay(overlayMesh);
            overlayMesh.Complete();
        }
        catch (Exception exception)
        {
            overlayMesh.Clear();
            Console.Error.WriteLine($"UIControl {GetType().Name} PopulateOverlay failed: {exception}");
        }
        ++overlayRevision;
        if (overlayRevision == 0) overlayRevision = 1;
        return overlayMesh;
    }

    /// <summary>本控件是否有附加内容；默认没有，派生控件重写它来声明。</summary>
    protected virtual bool HasOverlay => false;

    /// <summary>检查附加网格引用的外部资源是否失效。</summary>
    protected virtual bool IsOverlayInvalidated() => false;

    /// <summary>生成附加网格；派生控件在这里画文本、选区、光标一类的内容。</summary>
    protected virtual void PopulateOverlay(UIMeshBuilder mesh)
    {
    }

    /// <summary>标记附加网格需要重建。</summary>
    protected void SetOverlayDirty()
    {
        overlayDirty = true;
    }

    //状态色 = 禁用 ? disabled : 按下 ? pressed : 悬停 ? hover : normal。
    internal void RefreshVisualState()
    {
        UINode? node = UIWorldContext.Current?.FindNode(EnsId);
        UIVisual? visual = this is Button ? node?.GetElement<Image>() : node?.Visual;
        if (visual == null) return;

        color state = !CanInteract() ? disabledColor
            : pressed ? pressedColor
            : hovered ? hoverColor
            : normalColor;
        visual.SetStateMultiplier(state);
    }

}
