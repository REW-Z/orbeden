namespace OrbedenEditor;

/// <summary>
/// 创建菜单里的控件种类。数值按创建菜单表的展开顺序从 0 赋值，
/// CheckBox 与 RadioButton 各占一项；这是持久化与脚本可见的合同，只能追加。
/// </summary>
public enum UIWidgetKind : uint
{
    /// <summary>画布。</summary>
    Canvas = 0,

    /// <summary>图片。</summary>
    Image = 1,

    /// <summary>文本。</summary>
    Text = 2,

    /// <summary>按钮。</summary>
    Button = 3,

    /// <summary>复选框。</summary>
    CheckBox = 4,

    /// <summary>单选框。</summary>
    RadioButton = 5,

    /// <summary>滑动条。</summary>
    Slider = 6,

    /// <summary>滚动条。</summary>
    ScrollBar = 7,

    /// <summary>滚动容器。</summary>
    ScrollBox = 8,

    /// <summary>下拉框。</summary>
    ComboBox = 9,

    /// <summary>文本输入框。</summary>
    TextField = 10,

    /// <summary>布局盒。</summary>
    LayoutBox = 11,

    /// <summary>网格盒。</summary>
    GridBox = 12,

    /// <summary>遮罩。</summary>
    Mask = 13,

    /// <summary>世界空间画布：800×600、节点 Transform 缩放 0.01。</summary>
    WorldSpaceCanvas = 14,
}
