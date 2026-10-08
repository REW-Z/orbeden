using System;

namespace Orbeden;

/// <summary>
/// 复选框。点击或提交切换勾选状态，勾选标记按状态做运行时可见性覆盖：
/// 只影响绘制与命中，不改标记自己的启用状态，控件销毁时撤销覆盖。
/// </summary>
public class CheckBox : UIControl
{
    [SerializeField] private bool isChecked;
    [SerializeField] private UIVisual? checkmark;

    /// <summary>勾选状态变化。派发时触发；同值不通知。</summary>
    public event Action<bool>? CheckedChanged;

    /// <summary>创建复选框组件包装。</summary>
    public CheckBox(Ens ens) : base(ens)
    {
        ApplyCheckmarkVisibility();
    }

    /// <summary>当前是否勾选。</summary>
    public bool GetChecked() => isChecked;

    /// <summary>勾选标记；为空时只维护状态，不改变任何可见性。</summary>
    public UIVisual? GetCheckmark() => checkmark;

    /// <summary>设置勾选标记，并按当前状态刷新它的可见性。</summary>
    public void SetCheckmark(UIVisual? value)
    {
        if (ReferenceEquals(checkmark, value)) return;
        //旧标记的覆盖要先撤掉，否则它会一直停在上一次的状态上。
        checkmark?.ClearRuntimeVisible(this);
        checkmark = value;
        ApplyCheckmarkVisibility();
    }

    /// <summary>
    /// 设置勾选状态。先更新值与标记可见性，再排事件；值没变就不通知。
    /// </summary>
    public void SetChecked(bool value, bool notify = true)
    {
        if (isChecked == value) return;
        isChecked = value;
        ApplyCheckmarkVisibility();
        if (notify) RaiseEvent(UIEventIds.CheckedChanged, UIEventPayload.Flagged(isChecked));
    }

    /// <summary>指针抬起：仍处于按下状态即切换一次。</summary>
    public override void OnPointerUp(in UIPointerEvent input)
    {
        if (!IsPressed()) return;
        Toggle();
    }

    /// <summary>提交：键盘或手柄的确认键同样切换。</summary>
    public override void OnSubmit() => Toggle();

    /// <summary>代码事件在派发时触发。</summary>
    protected override void RaiseCodeEvent(int eventId, in UIEventPayload payload)
    {
        if (eventId == UIEventIds.CheckedChanged) CheckedChanged?.Invoke(payload.flagged);
    }

    /// <summary>组件停用：撤销标记上的可见性覆盖，并放开本控件持有的捕获与焦点。</summary>
    protected override void OnUIDisabled()
    {
        checkmark?.ClearRuntimeVisible(this);
        UIWorldContext.Current?.InputRouter.CancelNode(EnsId);
    }

    /// <summary>控件摘除：同样撤销覆盖与捕获。</summary>
    protected override void OnUIDetached()
    {
        checkmark?.ClearRuntimeVisible(this);
        UIWorldContext.Current?.InputRouter.CancelNode(EnsId);
    }

    private void Toggle() => SetChecked(!isChecked);

    //标记的可见性由本控件持续覆盖；标记自己的启用状态不参与。
    private void ApplyCheckmarkVisibility()
    {
        checkmark?.SetRuntimeVisible(isChecked, this);
    }
}
