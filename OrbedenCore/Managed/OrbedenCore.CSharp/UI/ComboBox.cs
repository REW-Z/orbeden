using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 下拉框。选项是一列字符串，选中项显示在 label 上；弹层用同画布的临时子树，
/// 不创建嵌套画布。选项一变就关闭弹层，避免列表与数据对不上。
/// </summary>
public class ComboBox : UIControl
{
    [SerializeField] private List<string> options = [];
    [SerializeField] private int selectedIndex = -1;
    [SerializeField] private Text? label;
    [SerializeField] private float popupHeight = 180.0f;
    [SerializeField] private float itemHeight = 30.0f;

    /// <summary>选中项变化。派发时触发；同值不通知。</summary>
    public event Action<int>? SelectionChanged;

    private UIComboPopup? popup;

    /// <summary>创建下拉框组件包装。</summary>
    public ComboBox(Ens ens) : base(ens)
    {
    }

    /// <summary>选项数量。</summary>
    public int GetOptionCount() => options.Count;

    /// <summary>取指定下标的选项文本；越界抛 ArgumentOutOfRangeException。</summary>
    public string GetOption(int index)
    {
        if (index < 0 || index >= options.Count) throw new ArgumentOutOfRangeException(nameof(index));
        return options[index];
    }

    /// <summary>在指定位置插入选项；null 归一为空串。索引范围为 [0, Count]。</summary>
    public void InsertOption(int index, string text)
    {
        if (index < 0 || index > options.Count) throw new ArgumentOutOfRangeException(nameof(index));
        options.Insert(index, text ?? string.Empty);
        //插入点前面的项保持不变，后面的项整体后移。
        if (selectedIndex >= index) ++selectedIndex;
        ClosePopup();
        RefreshLabel();
    }

    /// <summary>改写指定下标的选项文本；null 归一为空串。</summary>
    public void SetOption(int index, string text)
    {
        if (index < 0 || index >= options.Count) throw new ArgumentOutOfRangeException(nameof(index));
        options[index] = text ?? string.Empty;
        ClosePopup();
        RefreshLabel();
    }

    /// <summary>删除指定下标的选项。</summary>
    public void RemoveOption(int index)
    {
        if (index < 0 || index >= options.Count) throw new ArgumentOutOfRangeException(nameof(index));
        options.RemoveAt(index);

        if (options.Count == 0)
        {
            //空表没有可选项。
            SetSelectedIndex(-1);
        }
        else if (index == selectedIndex)
        {
            //删掉的就是当前项：顶上来的那一项接位，越界取最后一项。
            SetSelectedIndex(Math.Min(index, options.Count - 1));
        }
        else if (index < selectedIndex)
        {
            //删除前面的项：当前条目不变，只是下标前移。
            --selectedIndex;
        }
        ClosePopup();
        RefreshLabel();
    }

    /// <summary>清空全部选项；选中项归为 -1。</summary>
    public void ClearOptions()
    {
        options.Clear();
        SetSelectedIndex(-1);
        ClosePopup();
        RefreshLabel();
    }

    /// <summary>当前选中下标；-1 表示没有选中。</summary>
    public int GetSelectedIndex() => selectedIndex;

    /// <summary>设置选中下标；允许 -1。非空选择会更新标签再排事件。</summary>
    public void SetSelectedIndex(int index, bool notify = true)
    {
        int clamped = index < 0 || index >= options.Count ? -1 : index;
        if (clamped == selectedIndex) return;

        selectedIndex = clamped;
        RefreshLabel();
        if (notify) RaiseEvent(UIEventIds.SelectionChanged, UIEventPayload.Index(selectedIndex));
    }

    /// <summary>标签文本组件。</summary>
    public Text? GetLabel() => label;

    /// <summary>设置标签文本组件，并按当前选中项刷新它。</summary>
    public void SetLabel(Text? value)
    {
        label = value;
        RefreshLabel();
    }

    /// <summary>弹层高度。</summary>
    public float GetPopupHeight() => popupHeight;

    /// <summary>设置弹层高度。</summary>
    public void SetPopupHeight(float value)
    {
        if (!float.IsFinite(value) || value <= 0.0f) return;
        popupHeight = value;
    }

    /// <summary>每行高度。</summary>
    public float GetItemHeight() => itemHeight;

    /// <summary>设置每行高度。</summary>
    public void SetItemHeight(float value)
    {
        if (!float.IsFinite(value) || value <= 0.0f) return;
        itemHeight = value;
    }

    /// <summary>弹层是否打开。</summary>
    public bool IsOpen() => popup?.IsOpen == true;

    /// <summary>打开弹层；重复调用无副作用。</summary>
    public void Open()
    {
        if (!CanInteract() || options.Count == 0) return;
        if (IsOpen()) return;

        UIWorldContext? context = UIWorldContext.Current;
        UINode? self = context?.FindNode(EnsId);
        UINode? canvasRoot = ResolveCanvasRoot(self);
        if (context == null || self?.Layout == null || canvasRoot == null) return;

        //同一个画布里只允许一个弹层。
        context.CloseComboPopups(canvasRoot.Canvas);

        popup ??= new UIComboPopup();
        UIRect anchor = self.Layout.GetResolvedRect();
        if (!popup.Open(this, options, selectedIndex, canvasRoot, anchor, itemHeight, popupHeight))
        {
            popup = null;
            return;
        }
        context.RegisterComboPopup(canvasRoot.Canvas, popup);
    }

    /// <summary>关闭弹层；重复调用无副作用。</summary>
    public void Close()
    {
        if (popup == null) return;
        UIWorldContext.Current?.UnregisterComboPopup(popup);
        popup.Close();
    }

    /// <summary>指针抬起：切换弹层开关。</summary>
    public override void OnPointerUp(in UIPointerEvent input)
    {
        if (!IsPressed()) return;
        if (IsOpen()) Close();
        else Open();
    }

    /// <summary>提交：同样切换弹层。</summary>
    public override void OnSubmit()
    {
        if (IsOpen()) Close();
        else Open();
    }

    /// <summary>取消：收起弹层并把焦点还给自己。</summary>
    public override void OnCancel() => Close();

    /// <summary>方向导航：弹层打开时在选项间移动。</summary>
    public override bool OnNavigate(UINavigation direction)
    {
        if (direction != UINavigation.Up && direction != UINavigation.Down) return false;
        if (options.Count == 0) return true;

        int step = direction == UINavigation.Down ? 1 : -1;
        int next = Math.Clamp(selectedIndex + step, 0, options.Count - 1);
        SetSelectedIndex(next);
        return true;
    }

    /// <summary>代码事件在派发时触发。</summary>
    protected override void RaiseCodeEvent(int eventId, in UIEventPayload payload)
    {
        if (eventId == UIEventIds.SelectionChanged) SelectionChanged?.Invoke(payload.index);
    }

    /// <summary>控件停用或摘除：收起弹层，别把临时子树留在场上。</summary>
    protected override void OnUIDisabled() => Close();

    /// <summary>控件摘除：收起弹层。</summary>
    protected override void OnUIDetached()
    {
        Close();
        //弹层已经销毁，这里的引用一并清掉。
        popup = null;
        UIWorldContext.Current?.InputRouter.CancelNode(EnsId);
    }

    //选项变了就关掉弹层：列表内容与数据必须一致。
    private void ClosePopup() => Close();

    private void RefreshLabel()
    {
        if (label == null) return;
        string text = selectedIndex >= 0 && selectedIndex < options.Count ? options[selectedIndex] : string.Empty;
        label.SetText(text);
    }

    //弹层挂在画布根下：不创建嵌套画布。
    private static UINode? ResolveCanvasRoot(UINode? node)
    {
        for (UINode? current = node; current != null; current = current.Parent)
        {
            if (current.Canvas != null) return current;
        }
        return null;
    }
}
