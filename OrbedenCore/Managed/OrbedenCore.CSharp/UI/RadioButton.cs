using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 单选框。同一分组里只应有一个被选中：选中一项时先把全组的标记更新一遍，
/// 再按层级顺序给其它项发 false，最后给自己发 true。
/// 点击已选中项保持选中；代码可以把选中项设为 false，分组因此可以为空。
/// </summary>
public class RadioButton : UIControl
{
    [SerializeField] private bool isChecked;
    [SerializeField] private UIVisual? checkmark;
    [SerializeField] private EnsId groupRoot;

    /// <summary>勾选状态变化。派发时触发；同值不通知。</summary>
    public OrbEvent<bool> CheckedChanged = new();

    /// <summary>创建单选框组件包装。</summary>
    public RadioButton(Ens ens) : base(ens)
    {
        ApplyCheckmarkVisibility();
    }

    /// <summary>当前是否选中。</summary>
    public bool GetChecked() => isChecked;

    /// <summary>勾选标记。</summary>
    public UIVisual? GetCheckmark() => checkmark;

    /// <summary>设置勾选标记，并按当前状态刷新可见性。</summary>
    public void SetCheckmark(UIVisual? value)
    {
        if (ReferenceEquals(checkmark, value)) return;
        checkmark?.ClearRuntimeVisible(this);
        checkmark = value;
        ApplyCheckmarkVisibility();
    }

    /// <summary>分组根；未设置时取直接父节点，没有父节点则取画布根。</summary>
    public EnsId GetGroupRoot() => groupRoot;

    /// <summary>设置分组根。</summary>
    public void SetGroupRoot(EnsId value)
    {
        if (groupRoot.id == value.id && groupRoot.version == value.version) return;
        groupRoot = value;
    }

    /// <summary>
    /// 设置选中状态。选中时先更新全组标记，再按层级顺序通知其它项 false，最后通知自己 true。
    /// </summary>
    public void SetChecked(bool value, bool notify = true)
    {
        if (isChecked == value) return;

        if (value)
        {
            //选中的路径要先把同组其它项放开，否则会出现两个选中的项。
            List<RadioButton> group = [];
            CollectGroup(group);
            foreach (RadioButton other in group)
            {
                if (ReferenceEquals(other, this)) continue;
                other.isChecked = false;
                other.ApplyCheckmarkVisibility();
            }

            isChecked = true;
            ApplyCheckmarkVisibility();

            //先按层级顺序发其它项的 false，最后发自己的 true。
            foreach (RadioButton other in group)
            {
                if (ReferenceEquals(other, this)) continue;
                if (notify) other.RaiseCheckedChanged();
            }
            if (notify) RaiseCheckedChanged();
            return;
        }

        //取消选中：分组可以为空，只通知自己。
        isChecked = false;
        ApplyCheckmarkVisibility();
        if (notify) RaiseCheckedChanged();
    }

    /// <summary>指针抬起：仍处于按下状态时选中自己。</summary>
    public override void OnPointerUp(in UIPointerEvent input)
    {
        if (!IsPressed()) return;
        //点击已选中项保持选中。
        if (isChecked) return;
        SetChecked(true);
    }

    /// <summary>提交：同样选中自己。</summary>
    public override void OnSubmit()
    {
        if (isChecked) return;
        SetChecked(true);
    }

    /// <summary>代码事件在派发时触发。</summary>
    protected override void DispatchEvent(int eventId, in UIEventPayload payload)
    {
        if (eventId == UIEventIds.CheckedChanged) CheckedChanged.Dispatch(payload.flagged);
    }

    /// <summary>组件挂载：多项选中时保留配置、只让层级最前的一项生效。</summary>
    protected override void OnUIAttached() => ResolveConflicts();

    /// <summary>换了父级就换了分组：重新登记后把新组里的冲突再解一次。</summary>
    protected override void OnUIReparented() => ResolveConflicts();

    /// <summary>组件停用：撤销标记上的可见性覆盖，并放开本控件持有的捕获与焦点。</summary>
    protected override void OnUIDisabled()
    {
        checkmark?.ClearRuntimeVisible(this);
        UIWorldContext.Current?.InputRouter.CancelNode(EnsId);
    }

    /// <summary>控件摘除：撤销标记上属于本控件的可见性覆盖。</summary>
    protected override void OnUIDetached()
    {
        checkmark?.ClearRuntimeVisible(this);
        UIWorldContext.Current?.InputRouter.CancelNode(EnsId);
    }

    //配置里可能有多项选中：保留配置，运行状态只让层级最前的一项生效，不标脏场景。
    private void ResolveConflicts()
    {
        List<RadioButton> group = [];
        CollectGroup(group);
        bool foundChecked = false;
        foreach (RadioButton item in group)
        {
            if (!item.isChecked) continue;
            //按层级顺序遍历，第一项保留选中，其余在运行状态里被放开。
            if (foundChecked) item.isChecked = false;
            else foundChecked = true;
            item.ApplyCheckmarkVisibility();
        }
    }

    //收集同组控件：从分组根出发按层级顺序遍历整棵子树。
    private void CollectGroup(List<RadioButton> result)
    {
        result.Clear();
        UINode? root = ResolveGroupNode();
        if (root == null)
        {
            result.Add(this);
            return;
        }
        CollectGroup(root, result);
    }

    private static void CollectGroup(UINode node, List<RadioButton> result)
    {
        if (node.GetElement<RadioButton>() is RadioButton radio) result.Add(radio);
        foreach (UINode child in node.Children) CollectGroup(child, result);
    }

    //分组根：显式设置优先；否则取直接父节点；没有父节点取画布根。
    private UINode? ResolveGroupNode()
    {
        UIWorldContext? context = UIWorldContext.Current;
        if (context == null) return null;

        if (groupRoot.id != 0)
        {
            UINode? explicitRoot = context.FindNode(groupRoot);
            if (explicitRoot != null) return explicitRoot;
        }

        UINode? self = context.FindNode(EnsId);
        if (self?.Parent != null) return self.Parent;
        return self?.Canvas != null ? self : null;
    }

    //标记可见性由本控件持续覆盖。
    private void ApplyCheckmarkVisibility()
    {
        checkmark?.SetRuntimeVisible(isChecked, this);
    }

    private void RaiseCheckedChanged() =>
        RaiseEvent(UIEventIds.CheckedChanged, UIEventPayload.Flagged(isChecked));
}
