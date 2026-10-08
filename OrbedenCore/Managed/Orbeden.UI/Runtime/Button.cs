using System;

namespace Orbeden;

/// <summary>
/// 按钮。指针在同目标上抬起，或拿到焦点后提交，都算一次点击。
/// 点击事件统一进派发器：持久化绑定先执行，随后才是代码事件与代码订阅。
/// </summary>
public class Button : UIControl
{
    /// <summary>点击。派发时触发，不在指针回调里直接触发。</summary>
    public event Action? Clicked;

    /// <summary>创建按钮组件包装。</summary>
    public Button(Ens ens) : base(ens)
    {
    }

    /// <summary>指针抬起：只有仍处于按下状态才算点击——离开目标时按下状态已经被清掉。</summary>
    public override void OnPointerUp(in UIPointerEvent input)
    {
        if (!IsPressed()) return;
        RaiseClicked();
    }

    /// <summary>提交：键盘或手柄的确认键。</summary>
    public override void OnSubmit() => RaiseClicked();

    /// <summary>代码事件在派发时触发，与绑定、代码订阅共用同一次派发顺序。</summary>
    protected override void RaiseCodeEvent(int eventId, in UIEventPayload payload)
    {
        if (eventId == UIEventIds.Clicked) Clicked?.Invoke();
    }

    private void RaiseClicked() => RaiseEvent(UIEventIds.Clicked, default);
}
