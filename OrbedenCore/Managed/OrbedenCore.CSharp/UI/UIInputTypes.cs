using System;

namespace Orbeden;

/// <summary>一条原始输入事件：跨语言记录加它的文本。</summary>
public readonly record struct UIRawInputEvent(UIInputRecord data, string text)
{
    /// <summary>事件种类。</summary>
    public UIInputKind Kind => (UIInputKind)data.kind;

    /// <summary>设备。</summary>
    public UIInputDevice Device => (UIInputDevice)data.device;

    /// <summary>序列号；消费时按它确认。</summary>
    public ulong Sequence => data.sequence;
}

/// <summary>指针事件的阶段。</summary>
public enum UIPointerPhase : uint
{
    /// <summary>按下。</summary>
    Down = 0,

    /// <summary>移动。</summary>
    Move = 1,

    /// <summary>抬起。</summary>
    Up = 2,

    /// <summary>取消：失焦、隐藏、销毁、视图切换。</summary>
    Cancel = 3,

    /// <summary>滚轮；增量放在 delta 里。</summary>
    Scroll = 4,
}

/// <summary>一次指针事件。position 是窗口逻辑坐标；viewId 指出它命中的视图，0 表示还没判过。</summary>
public struct UIPointerEvent
{
    /// <summary>指针标识；0 为鼠标，触摸从 1 开始。</summary>
    public uint pointerId;

    /// <summary>阶段。</summary>
    public UIPointerPhase phase;

    /// <summary>按键；触摸固定左键。</summary>
    public uint button;

    /// <summary>窗口逻辑坐标。</summary>
    public vector2 position;

    /// <summary>本次增量。</summary>
    public vector2 delta;

    /// <summary>平台时间戳，秒。</summary>
    public double timestamp;

    /// <summary>命中的视图；未判定时为 0。</summary>
    public ulong viewId;

    /// <summary>来源事件序列。</summary>
    public ulong sequence;
}

/// <summary>一次命中的结果。</summary>
public struct UIHitResult
{
    /// <summary>命中的图形。</summary>
    public UIVisual? visual;

    /// <summary>命中图形所在节点上的控件；没有控件时为空。</summary>
    public UIControl? control;

    /// <summary>命中的画布。</summary>
    public Canvas? canvas;

    /// <summary>命中点在该图形局部空间的坐标。</summary>
    public vector2 localPoint;

    /// <summary>参与判定的呈现序号；深度比较必须用同一序号。</summary>
    public ulong presentedFrame;

    /// <summary>是否有效命中。</summary>
    public readonly bool IsValid => visual != null;
}

/// <summary>一路指针的状态。</summary>
public struct UIPointerState
{
    /// <summary>按下时命中的处理目标；没有处理者时无效但依然占用序列。</summary>
    public UIEventTarget downTarget;

    /// <summary>当前捕获指针的处理目标。</summary>
    public UIEventTarget captureTarget;

    /// <summary>按下位置。</summary>
    public vector2 pressPosition;

    /// <summary>上次位置。</summary>
    public vector2 lastPosition;

    /// <summary>是否已经越过拖动阈值。</summary>
    public bool dragging;

    /// <summary>按下的按键。</summary>
    public uint button;

    /// <summary>是否处于按下状态。</summary>
    public bool active;

    /// <summary>本指针是否已被 UI 消费；消费后直到抬起都不进入游戏查询。</summary>
    public bool consumed;

    /// <summary>按下时的来源事件序列；取消时据此记住这次按下已经作废。</summary>
    public ulong pressSequence;
}

/// <summary>
/// 一次指针交互的处理目标：稳定的处理节点身份，加上该节点上的控件（负责按下状态与导航）。
/// 普通组件也能成为处理目标，因此没有控件时事件不会丢。
/// </summary>
public readonly struct UIEventTarget
{
    /// <summary>处理节点的 Ens。</summary>
    public readonly EnsId ens;

    /// <summary>节点上可交互的控件；没有时为空。</summary>
    public readonly UIControl? control;

    /// <summary>创建处理目标。</summary>
    public UIEventTarget(EnsId ens, UIControl? control)
    {
        this.ens = ens;
        this.control = control;
    }

    /// <summary>是否指向了一个节点。</summary>
    public bool IsValid => !ens.IsNull && ens.version != 0;

    /// <summary>节点是否还在 UI 树里；控件被销毁不算失效。</summary>
    public bool IsAlive => IsValid && UIWorldContext.Current?.FindNode(ens) != null;
}

/// <summary>方向导航。</summary>
public enum UINavigation : uint
{
    /// <summary>上。</summary>
    Up = 0,

    /// <summary>下。</summary>
    Down = 1,

    /// <summary>左。</summary>
    Left = 2,

    /// <summary>右。</summary>
    Right = 3,
}
