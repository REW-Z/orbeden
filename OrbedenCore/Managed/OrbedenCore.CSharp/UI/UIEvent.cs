namespace Orbeden;

/// <summary>控件事件的标识；数值是持久化合同的一部分，只能追加。</summary>
public static class UIEventIds
{
    /// <summary>Button.Clicked。</summary>
    public const int Clicked = 0;

    /// <summary>CheckBox/RadioButton.CheckedChanged，值在 flagged 里。</summary>
    public const int CheckedChanged = 1;

    /// <summary>Slider/ScrollBar.ValueChanged，值在 value 里。</summary>
    public const int ValueChanged = 2;

    /// <summary>ScrollBox.ScrollChanged，值在 point 里。</summary>
    public const int ScrollChanged = 3;

    /// <summary>ComboBox.SelectionChanged，值在 index 里。</summary>
    public const int SelectionChanged = 4;

    /// <summary>TextField.TextChanged，值在 text 里。</summary>
    public const int TextChanged = 5;

    /// <summary>TextField.Submitted，值在 text 里。</summary>
    public const int Submitted = 6;

    //按事件标识给出稳定名字与载荷种类；编辑器的事件表用它们挑可绑定的方法。
    private static readonly (int Id, string Name, InteropValueKind Payload)[] Entries =
    [
        (Clicked, nameof(Clicked), InteropValueKind.Empty),
        (CheckedChanged, nameof(CheckedChanged), InteropValueKind.Bool),
        (ValueChanged, nameof(ValueChanged), InteropValueKind.Float32),
        (ScrollChanged, nameof(ScrollChanged), InteropValueKind.Vector2),
        (SelectionChanged, nameof(SelectionChanged), InteropValueKind.Int32),
        (TextChanged, nameof(TextChanged), InteropValueKind.String),
        (Submitted, nameof(Submitted), InteropValueKind.String),
    ];

    /// <summary>事件标识的数量。</summary>
    public static int Count => Entries.Length;

    /// <summary>按下标取事件标识；越界返回 -1。</summary>
    public static int IdAt(int index) => index >= 0 && index < Entries.Length ? Entries[index].Id : -1;

    /// <summary>取事件标识的显示名；未知标识返回整数字符串。</summary>
    public static string GetName(int eventId)
    {
        foreach ((int id, string name, _) in Entries)
        {
            if (id == eventId) return name;
        }
        return eventId.ToString();
    }

    /// <summary>取事件载荷的种类；没有载荷（如 Clicked）与未知标识都返回 Empty。</summary>
    public static InteropValueKind GetPayloadKind(int eventId)
    {
        foreach ((int id, _, InteropValueKind payload) in Entries)
        {
            if (id == eventId) return payload;
        }
        return InteropValueKind.Empty;
    }
}

/// <summary>
/// 事件载荷。不同事件用不同字段，未使用的字段保持默认值；
/// 这样队列里装的是同一种定长记录，派发时不必按类型分支。
/// </summary>
public struct UIEventPayload
{
    /// <summary>布尔载荷；勾选变化用它。</summary>
    public bool flagged;

    /// <summary>数值载荷；滑条与滚动条用它。</summary>
    public float value;

    /// <summary>整数载荷；下拉选择用它。</summary>
    public int index;

    /// <summary>二维点载荷；滚动位移用它。</summary>
    public vector2 point;

    /// <summary>文本载荷；只读引用，派发时保持有效。</summary>
    public string? text;

    /// <summary>构造布尔载荷。</summary>
    public static UIEventPayload Flagged(bool value) => new() { flagged = value };

    /// <summary>构造数值载荷。</summary>
    public static UIEventPayload Number(float value) => new() { value = value };

    /// <summary>构造索引载荷。</summary>
    public static UIEventPayload Index(int value) => new() { index = value };

    /// <summary>构造点载荷。</summary>
    public static UIEventPayload Point(vector2 value) => new() { point = value };

    /// <summary>构造文本载荷。</summary>
    public static UIEventPayload Text(string value) => new() { text = value };
}

/// <summary>
/// 事件源。控件实现它；判定存活与代次是派发器的职责，
/// 因此这里不要求一定是 UIControl，用例可以换成假源。
/// </summary>
public interface IUIEventSource
{
    /// <summary>对象是否仍然存活。</summary>
    bool IsAlive { get; }

    /// <summary>所属 Ens。</summary>
    EnsId EnsId { get; }

    /// <summary>派发到本源时的第一步。</summary>
    void OnEvent(in UIEvent entry);

    /// <summary>事件在队列里被丢弃时调用。</summary>
    void OnEventDiscarded(in UIEvent entry);
}

/// <summary>一条 UI 事件。源与目标的代次在入队时记下，派发前逐一核对。</summary>
public readonly struct UIEvent
{
    /// <summary>入队序号；同帧内单调递增，保留入队顺序。</summary>
    public readonly ulong sequence;

    /// <summary>事件源。</summary>
    public readonly IUIEventSource source;

    /// <summary>发出时的代次；派发前与本体的当前代次核对。</summary>
    public readonly uint sourceVersion;

    /// <summary>事件目标；通常与源相同，转发时不同。</summary>
    public readonly EnsId target;

    /// <summary>事件标识，取 UIEventIds。</summary>
    public readonly int eventId;

    /// <summary>事件载荷。</summary>
    public readonly UIEventPayload payload;

    /// <summary>创建一条事件。</summary>
    public UIEvent(ulong sequence, IUIEventSource source, in UIEventPayload payload, int eventId, in EnsId target)
    {
        this.sequence = sequence;
        this.source = source;
        sourceVersion = source.EnsId.version;
        this.target = target;
        this.eventId = eventId;
        this.payload = payload;
    }

    /// <summary>事件源是否还是入队时的那个实例。</summary>
    public readonly bool HasValidSource() =>
        source.IsAlive && source.EnsId.version == sourceVersion;
}
