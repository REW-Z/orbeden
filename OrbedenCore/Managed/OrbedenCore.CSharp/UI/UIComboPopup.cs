using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 下拉弹层：同一画布下的临时子树（DontSave），里面是一个 ScrollBox 加一列选项按钮。
/// 不创建嵌套画布；关闭时先退订按钮事件，再销毁临时对象，最后把焦点还给原控件。
/// </summary>
public sealed class UIComboPopup
{
    //弹层根与它的子树；关闭时整棵销毁。
    private readonly List<Ens> created = [];
    private readonly List<Button> optionButtons = [];
    private readonly List<string> optionTexts = [];

    private Ens root;
    private ScrollBox? scrollBox;
    private UILayout? content;

    /// <summary>弹层所属的源控件。</summary>
    public ComboBox? Owner { get; private set; }

    /// <summary>弹层是否处于打开状态。</summary>
    public bool IsOpen => root.IsValid;

    /// <summary>最近一次建立弹层时使用的选项数。</summary>
    public int OptionCount => optionButtons.Count;

    /// <summary>
    /// 在画布下建立弹层。anchor 是源控件的解析矩形（画布空间），
    /// 默认向下展开，下方不足且上方更大时向上，最后夹紧画布根矩形。
    /// </summary>
    public bool Open(ComboBox owner, IReadOnlyList<string> options, int selectedIndex,
        UINode canvasRoot, UIRect anchor, float itemHeight, float popupHeight)
    {
        Close();
        if (owner == null || canvasRoot == null) return false;

        Owner = owner;
        UIRect canvasRect = canvasRoot.Layout?.GetResolvedRect() ?? default;
        float height = MathF.Min(popupHeight, MathF.Max(itemHeight, options.Count * itemHeight));

        //默认向下；下方放不下且上方更大就翻到上面。
        float below = canvasRect.min.y + canvasRect.Height - anchor.min.y;
        float above = anchor.Max.y - canvasRect.min.y;
        bool upward = below < height && above > below;
        float minY = upward ? anchor.Max.y - height : anchor.min.y;
        //夹紧到画布根矩形里。
        minY = Math.Clamp(minY, canvasRect.min.y, MathF.Max(canvasRect.min.y, canvasRect.Max.y - height));

        UIRect popupRect = new(new vector2(anchor.min.x, minY), new vector2(anchor.Width, height));
        BuildTree(canvasRoot, popupRect, options, selectedIndex, itemHeight);
        return root.IsValid;
    }

    /// <summary>关闭弹层：先退订，再销毁，最后恢复有效焦点。</summary>
    public void Close()
    {
        ComboBox? owner = Owner;
        Owner = null;

        //退订要用当初挂上去的那个委托，按序号重建同一个实例。
        for (int index = 0; index < optionButtons.Count; ++index)
        {
            Button button = optionButtons[index];
            int captured = index;
            button.Clicked -= MakeHandler(captured);
        }
        optionButtons.Clear();
        optionTexts.Clear();

        foreach (Ens ens in created)
        {
            if (ens.IsValid) ens.Destroy();
        }
        created.Clear();
        root = default;
        scrollBox = null;
        content = null;

        //把焦点还给源控件，前提是它还在并且仍可交互。
        if (owner != null && owner.IsAlive && owner.CanInteract()) owner.Focus();
    }

    /// <summary>外部点击或取消：关闭并消费该序列。</summary>
    public void CloseAndConsume(ulong sequence)
    {
        Close();
        if (sequence == 0) return;
        UIWorldContext.Current?.InputRouter.ConsumeSequence(sequence);
    }

    //建树：弹层根 → 滚动容器 → 内容 → 每个选项一个按钮。
    private void BuildTree(UINode canvasRoot, UIRect rect, IReadOnlyList<string> options, int selectedIndex,
        float itemHeight)
    {
        root = CreateNode(canvasRoot.Ens);
        if (!root.IsValid) return;

        UILayout? rootLayout = root.AddComponent<UILayout>();
        if (rootLayout != null)
        {
            rootLayout.SetPivot(new vector2(0.0f, 0.0f));
            rootLayout.SetDrivenRect(rect, this);
        }
        //弹层要能挡住下面的内容，因此挂一层不透明的底图。
        Image? backdrop = root.AddComponent<Image>();
        backdrop?.SetRaycastTarget(true);

        scrollBox = root.AddComponent<ScrollBox>();
        content = CreateContent(root.Id, options.Count * itemHeight);
        if (content == null) return;
        scrollBox?.SetContent(content);

        for (int index = 0; index < options.Count; ++index)
        {
            CreateOption(content, index, options[index], itemHeight);
        }
    }

    private UILayout? CreateContent(EnsId parent, float totalHeight)
    {
        Ens node = CreateNode(parent);
        if (!node.IsValid) return null;

        UILayout? layout = node.AddComponent<UILayout>();
        if (layout != null)
        {
            //内容左上对齐、按总行高撑高：滚动范围由此得出。
            layout.SetAnchorMin(new vector2(0.0f, 1.0f));
            layout.SetAnchorMax(new vector2(1.0f, 1.0f));
            layout.SetPivot(new vector2(0.0f, 1.0f));
            layout.SetSizeDelta(new vector2(0.0f, totalHeight));
        }
        return layout;
    }

    private void CreateOption(UILayout contentLayout, int index, string text, float itemHeight)
    {
        Ens node = CreateNode(contentLayout.EnsId);
        if (!node.IsValid) return;

        UILayout? layout = node.AddComponent<UILayout>();
        if (layout != null)
        {
            //每行顶部对齐，按序号下移一个行高。
            layout.SetAnchorMin(new vector2(0.0f, 1.0f));
            layout.SetAnchorMax(new vector2(1.0f, 1.0f));
            layout.SetPivot(new vector2(0.0f, 1.0f));
            layout.SetSizeDelta(new vector2(0.0f, itemHeight));
            layout.SetOffset(new vector2(0.0f, -index * itemHeight));
        }

        node.AddComponent<Image>();
        Text? label = node.AddComponent<Text>();
        label?.SetText(text);

        Button? button = node.AddComponent<Button>();
        if (button == null) return;
        //按序号绑定：退订时按同样方式重建委托，才能摘干净。
        button.Clicked += MakeHandler(index);
        optionButtons.Add(button);
        optionTexts.Add(text);
    }

    //按选项下标构造点击处理委托；同一个下标两次调用产出可互相抵消的委托。
    private Action MakeHandler(int index) => () => OnOptionClicked(index);

    private void OnOptionClicked(int index)
    {
        ComboBox? owner = Owner;
        if (owner == null)
        {
            Close();
            return;
        }
        //点选后关闭再设值：先收起弹层，避免按钮在回调里被销毁后又被用到。
        Close();
        owner.SetSelectedIndex(index);
    }

    //父级用 EnsId：节点索引里存的就是它，不必先包成 Ens。
    private Ens CreateNode(EnsId parent)
    {
        Ens node = Ens.Create();
        if (!node.IsValid) return node;

        //临时对象不参与保存与复制。
        node.DontSave = true;
        if (!parent.IsNull) node.Transform.SetParent(parent);
        created.Add(node);
        return node;
    }

}
