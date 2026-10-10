using System;

namespace Orbeden;

/// <summary>
/// UI 节点的矩形配置与解析结果。配置持久化，解析结果只存在于运行时。
/// 轴心与锚点定义矩形的落位，Transform 局部 X/Y 提供偏移，sizeDelta 提供尺寸增量。
/// </summary>
[UniqueComponent]
public class UILayout : Script, IManagedComponentLifecycle
{
    /// <summary>锚点左下角，两个分量都限于 [0,1]。</summary>
    [SerializeField] private vector2 anchorMin = new(0.5f, 0.5f);

    /// <summary>锚点右上角，两个分量都限于 [0,1]。</summary>
    [SerializeField] private vector2 anchorMax = new(0.5f, 0.5f);

    /// <summary>轴心，两个分量都限于 [0,1]。</summary>
    [SerializeField] private vector2 pivot = new(0.5f, 0.5f);

    /// <summary>相对锚点区域尺寸的增量，可为负。</summary>
    [SerializeField] private vector2 sizeDelta = new(100.0f, 30.0f);

    /// <summary>宽度取测量期望值，忽略锚点区域给出的宽度。</summary>
    [SerializeField] private bool fitWidth;

    /// <summary>高度取测量期望值，忽略锚点区域给出的高度。</summary>
    [SerializeField] private bool fitHeight;

    /// <summary>不参与父布局组的排列。</summary>
    [SerializeField] private bool ignoreLayout;

    //解析缓存：由 UILayoutRegistry 在布局阶段写入，不持久化。
    private UIRect resolvedRect;
    private vector2 resolvedAnchorPoint;
    private vector2 publishedSize;
    private bool hasResolvedRect;
    //运行期矩形覆盖的来源与当前值；不参与持久化。
    private object? drivenOwner;
    private UIRect drivenRect;
    private bool hasDrivenRect;
    //运行期平移覆盖：叠加在解析结果之上，不替代解析。
    private object? drivenOffsetOwner;
    private vector2 drivenOffset;
    private bool hasDrivenOffset;

    /// <summary>创建布局组件包装。</summary>
    public UILayout(Ens ens) : base(ens)
    {
    }

    //登记布局组件引起的节点结构变化
    void IManagedComponentLifecycle.OnComponentAttached()
    {
        UIWorldContext.Current?.RequestFullResync();
    }

    //撤销派生位置并登记布局卸载
    void IManagedComponentLifecycle.OnComponentDetached()
    {
        UIWorldContext.ClearDerivedPosition(EnsId);
        UIWorldContext.Current?.RequestFullResync();
    }

    //刷新布局组件的活动状态
    void IManagedComponentLifecycle.OnComponentActiveChanged(bool active) => MarkLayoutDirty();

    //刷新恢复或批量应用的布局字段
    void IManagedComponentLifecycle.OnComponentFieldsChanged() => MarkLayoutDirty();

    /// <summary>锚点左下角。</summary>
    public vector2 GetAnchorMin() => anchorMin;

    /// <summary>设置锚点左下角；分量夹紧到 [0,1]，且不超过当前 anchorMax。</summary>
    public void SetAnchorMin(vector2 value)
    {
        vector2 clamped = ClampUnit(ClampPair(value));
        clamped.x = MathF.Min(clamped.x, anchorMax.x);
        clamped.y = MathF.Min(clamped.y, anchorMax.y);
        if (Same(clamped, anchorMin)) return;
        anchorMin = clamped;
        MarkLayoutDirty();
    }

    /// <summary>锚点右上角。</summary>
    public vector2 GetAnchorMax() => anchorMax;

    /// <summary>同时设置两个锚点；分量夹紧到单位区间并按左下、右上排序。</summary>
    public void SetAnchors(vector2 min, vector2 max)
    {
        min = ClampUnit(ClampPair(min));
        max = ClampUnit(ClampPair(max));
        vector2 lower = new(MathF.Min(min.x, max.x), MathF.Min(min.y, max.y));
        vector2 upper = new(MathF.Max(min.x, max.x), MathF.Max(min.y, max.y));
        if (Same(anchorMin, lower) && Same(anchorMax, upper)) return;
        anchorMin = lower;
        anchorMax = upper;
        MarkLayoutDirty();
    }

    /// <summary>设置锚点右上角；分量夹紧到 [0,1]，且不小于当前 anchorMin。</summary>
    public void SetAnchorMax(vector2 value)
    {
        vector2 clamped = ClampUnit(ClampPair(value));
        clamped.x = MathF.Max(clamped.x, anchorMin.x);
        clamped.y = MathF.Max(clamped.y, anchorMin.y);
        if (Same(clamped, anchorMax)) return;
        anchorMax = clamped;
        MarkLayoutDirty();
    }

    /// <summary>轴心。setter 不补偿位置：需要保持矩形时由编辑器同时改 Transform 局部位置。</summary>
    public vector2 GetPivot() => pivot;

    /// <summary>设置轴心；分量夹紧到 [0,1]，不改动 Transform 局部位置。</summary>
    public void SetPivot(vector2 value)
    {
        vector2 clamped = ClampUnit(ClampPair(value));
        if (Same(clamped, pivot)) return;
        pivot = clamped;
        MarkLayoutDirty();
    }

    /// <summary>读取 Transform 局部 X/Y 作为锚点偏移。</summary>
    public vector2 GetOffset()
    {
        vector3 position = Ens.Transform.GetLocalPosition();
        return new vector2(position.x, position.y);
    }

    /// <summary>设置 Transform 局部 X/Y，保留 Z；非有限值拒绝写入。</summary>
    public void SetOffset(vector2 value)
    {
        if (!UILayoutMath.IsFinite(value)) return;
        Transform transform = Ens.Transform;
        vector3 position = transform.GetLocalPosition();
        if (position.x == value.x && position.y == value.y) return;
        transform.SetLocalPosition(new vector3(value.x, value.y, position.z));
        MarkLayoutDirty();
    }

    //读取布局落点与 Transform 局部 Z
    internal vector3 GetResolvedLocalPosition()
    {
        vector3 position = Ens.Transform.GetLocalPosition();
        if (Ens.GetComponent<Canvas>() != null || !hasResolvedRect) return position;
        return new vector3(resolvedAnchorPoint.x, resolvedAnchorPoint.y, position.z);
    }

    /// <summary>尺寸增量。</summary>
    public vector2 GetSizeDelta() => sizeDelta;

    /// <summary>设置尺寸增量；非有限值拒绝写入，允许为负。</summary>
    public void SetSizeDelta(vector2 value)
    {
        if (!UILayoutMath.IsFinite(value) || Same(value, sizeDelta)) return;
        sizeDelta = value;
        MarkLayoutDirty();
    }

    /// <summary>宽度是否取测量期望值。</summary>
    public bool GetFitWidth() => fitWidth;

    /// <summary>设置宽度是否取测量期望值。</summary>
    public void SetFitWidth(bool value)
    {
        if (fitWidth == value) return;
        fitWidth = value;
        MarkLayoutDirty();
    }

    /// <summary>高度是否取测量期望值。</summary>
    public bool GetFitHeight() => fitHeight;

    /// <summary>设置高度是否取测量期望值。</summary>
    public void SetFitHeight(bool value)
    {
        if (fitHeight == value) return;
        fitHeight = value;
        MarkLayoutDirty();
    }

    /// <summary>是否忽略父布局组的排列。</summary>
    public bool GetIgnoreLayout() => ignoreLayout;

    /// <summary>设置是否忽略父布局组的排列；变化时同时让父组重排。</summary>
    public void SetIgnoreLayout(bool value)
    {
        if (ignoreLayout == value) return;
        ignoreLayout = value;
        UIWorldContext.NotifyLayoutStructureChanged(this);
    }

    /// <summary>读取上一帧解析出的矩形；尚未布局时返回零尺寸矩形。</summary>
    public UIRect GetResolvedRect() => hasResolvedRect ? resolvedRect : new UIRect(new vector2(0.0f, 0.0f), new vector2(0.0f, 0.0f));

    //判断当前是否已有解析结果。零尺寸的合法结果与“尚未布局”必须区分。
    internal bool HasResolvedRect => hasResolvedRect;

    /// <summary>读取解析出的锚点落点（设计公式里的 Q，位于父级局部空间）。</summary>
    public vector2 GetResolvedAnchorPoint() => resolvedAnchorPoint;

    //写入解析结果与锚点落点，由 UILayoutRegistry 与布局组调用。
    internal void SetResolvedRect(UIRect value, vector2 anchorPoint)
    {
        resolvedRect = value;
        resolvedAnchorPoint = anchorPoint;
        hasResolvedRect = true;
    }

    //取走“尺寸自上次取用后发生了变化”这一事实；布局重建后用它决定谁要重建几何。
    internal bool ConsumeSizeChanged()
    {
        if (!hasResolvedRect) return false;
        vector2 size = resolvedRect.size;
        if (size.x == publishedSize.x && size.y == publishedSize.y) return false;
        publishedSize = size;
        return true;
    }

    //清除解析结果，退出 UI 或世界分离时调用。
    internal void ClearResolvedRect()
    {
        resolvedRect = default;
        resolvedAnchorPoint = default;
        publishedSize = default;
        hasResolvedRect = false;
    }

    /// <summary>
    /// 按父矩形逐分量解析本节点。parentMin/parentSize 是父矩形在父级局部空间中的范围，
    /// 返回的矩形在本节点自身局部空间（左下角为 -pivot * size），anchorPoint 即设计公式里的 Q，
    /// 位于父级局部空间，供派生位置使用。
    /// </summary>
    public UIRect Resolve(vector2 parentMin, vector2 parentSize, out vector2 anchorPoint) =>
        UILayoutMath.Resolve(anchorMin, anchorMax, pivot, GetOffset(), sizeDelta, parentMin, parentSize, out anchorPoint);

    /// <summary>按父矩形解析并直接得到本节点局部矩形，丢弃锚点落点。</summary>
    public UIRect ResolveAgainst(vector2 parentMin, vector2 parentSize) => Resolve(parentMin, parentSize, out _);

    /// <summary>是否存在运行期矩形覆盖。</summary>
    public bool HasDrivenRect => hasDrivenRect;

    /// <summary>当前被驱动的矩形；没有被驱动时是零矩形。</summary>
    public UIRect GetDrivenRect() => drivenRect;

    /// <summary>
    /// 运行期矩形覆盖：控件驱动子布局（滑条拇指、填充、滚动内容）时用它，
    /// 只改运行结果，不写回锚点、偏移与尺寸这些配置。覆盖带来源，来源销毁或主动撤销即失效。
    /// rect 用的是**父级空间**：本节点局部空间的原点落在自身锚点上，换算在解析时完成。
    /// </summary>
    public void SetDrivenRect(in UIRect rect, object owner)
    {
        ArgumentNullException.ThrowIfNull(owner);
        drivenOwner = owner;
        drivenRect = rect;
        hasDrivenRect = true;
        MarkLayoutDirty();
    }

    /// <summary>是否存在运行期平移覆盖。</summary>
    public bool HasDrivenOffset => hasDrivenOffset;

    /// <summary>
    /// 运行期平移覆盖：在常规解析结果之上再平移一段，滚动容器用它推内容。
    /// 与矩形覆盖的区别是它不替代解析，因此可以叠加在锚点与尺寸配置之上。
    /// </summary>
    public void SetDrivenOffset(vector2 delta, object owner)
    {
        ArgumentNullException.ThrowIfNull(owner);
        drivenOffsetOwner = owner;
        drivenOffset = delta;
        hasDrivenOffset = true;
        MarkLayoutDirty();
    }

    /// <summary>当前被驱动的平移量；没有被驱动时是零向量。</summary>
    public vector2 GetDrivenOffset() => drivenOffset;

    /// <summary>撤销某个来源设下的平移覆盖；不是它设的就不动。</summary>
    public void ClearDrivenOffset(object owner)
    {
        if (!ReferenceEquals(drivenOffsetOwner, owner)) return;
        drivenOffsetOwner = null;
        drivenOffset = default;
        hasDrivenOffset = false;
        MarkLayoutDirty();
    }

    /// <summary>撤销某个来源设下的矩形覆盖；不是它设的就不动。</summary>
    public void ClearDrivenRect(object owner)
    {
        if (!ReferenceEquals(drivenOwner, owner)) return;
        drivenOwner = null;
        drivenRect = default;
        hasDrivenRect = false;
        MarkLayoutDirty();
    }

    //被驱动时按父级空间的覆盖矩形换算：锚点即 pivot 在矩形上的落点，
    //局部矩形于是以 -pivot*size 为原点，与常规解析的约定一致。
    internal UIRect ResolveDriven(out vector2 anchorPoint)
    {
        anchorPoint = new vector2(drivenRect.min.x + pivot.x * drivenRect.size.x,
            drivenRect.min.y + pivot.y * drivenRect.size.y);
        return new UIRect(new vector2(drivenRect.min.x - anchorPoint.x, drivenRect.min.y - anchorPoint.y),
            drivenRect.size);
    }

    //标记布局脏并上报到注册表。
    private void MarkLayoutDirty()
    {
        UIWorldContext.MarkLayoutDirty(this);
    }

    //把两个分量夹紧到 [0,1]。
    private static vector2 ClampUnit(vector2 value) =>
        new(Math.Clamp(value.x, 0.0f, 1.0f), Math.Clamp(value.y, 0.0f, 1.0f));

    //非有限分量换成默认值，避免污染解析公式。
    private static vector2 ClampPair(vector2 value) =>
        new(float.IsFinite(value.x) ? value.x : 0.5f, float.IsFinite(value.y) ? value.y : 0.5f);

    private static bool Same(vector2 left, vector2 right) => left.x == right.x && left.y == right.y;

}
