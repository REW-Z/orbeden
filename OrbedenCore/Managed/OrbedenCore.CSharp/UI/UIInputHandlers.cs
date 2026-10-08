namespace Orbeden;

/// <summary>
/// UI 输入处理接口。挂在节点上的任意托管组件都可以实现它们，不必继承 UIControl，
/// 也不必拥有可绘制网格。事件从命中节点向祖先寻找实现者，同一节点按组件顺序执行。
/// 接口本身不授予可导航资格：焦点与自动导航仍由 UIControl 维护。
/// </summary>
public interface IUIPointerDownHandler
{
    /// <summary>指针在本节点按下。</summary>
    void OnPointerDown(in UIPointerEvent input);
}

/// <summary>指针抬起的处理接口。</summary>
public interface IUIPointerUpHandler
{
    /// <summary>指针在本节点抬起；捕获仍在时即使指针已移出也会收到。</summary>
    void OnPointerUp(in UIPointerEvent input);
}

/// <summary>指针移动的处理接口。</summary>
public interface IUIPointerMoveHandler
{
    /// <summary>指针在本节点上移动，或本节点持有捕获时移动。</summary>
    void OnPointerMove(in UIPointerEvent input);
}

/// <summary>指针进入的处理接口。</summary>
public interface IUIPointerEnterHandler
{
    /// <summary>鼠标指针进入本节点；触摸不产生悬停。</summary>
    void OnPointerEnter(in UIPointerEvent input);
}

/// <summary>指针离开的处理接口。</summary>
public interface IUIPointerExitHandler
{
    /// <summary>鼠标指针离开本节点。</summary>
    void OnPointerExit(in UIPointerEvent input);
}

/// <summary>指针取消的处理接口。</summary>
public interface IUIPointerCancelHandler
{
    /// <summary>一路指针被取消：失焦、禁用、销毁或视图切换。</summary>
    void OnPointerCancel(in UIPointerEvent input);
}

/// <summary>滚轮的处理接口。</summary>
public interface IUIScrollHandler
{
    /// <summary>处理一次滚轮；返回真表示已消费，不再向祖先传播。</summary>
    bool OnScroll(vector2 delta);
}

/// <summary>方向导航的处理接口。</summary>
public interface IUINavigationHandler
{
    /// <summary>处理一次方向导航；返回真表示焦点不移动。</summary>
    bool OnNavigate(UINavigation direction);
}

/// <summary>提交的处理接口。</summary>
public interface IUISubmitHandler
{
    /// <summary>确认键或手柄提交键按下。</summary>
    void OnSubmit();
}

/// <summary>取消的处理接口。</summary>
public interface IUICancelHandler
{
    /// <summary>取消键按下。</summary>
    void OnCancel();
}

/// <summary>焦点变化的处理接口。</summary>
public interface IUIFocusHandler
{
    /// <summary>焦点进出本节点。</summary>
    void OnFocusChanged(bool focused);
}

/// <summary>
/// 命中过滤接口。图形或它的任一祖先实现它都可以否决一次命中：
/// 本地坐标由命中点换算到实现者所在节点的局部空间。过滤器不能绕过裁剪层与世界空间深度遮挡。
/// </summary>
public interface IUIRaycastFilter
{
    /// <summary>这个本地坐标是否算命中；返回假时继续检查下层图形。</summary>
    bool IsRaycastLocationValid(vector2 localPoint);
}
