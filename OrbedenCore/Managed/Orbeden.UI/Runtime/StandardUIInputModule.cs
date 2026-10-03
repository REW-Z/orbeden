using System;

namespace Orbeden;

/// <summary>
/// 默认输入模块：把平台事件翻译成指针、滚轮与导航。
/// 手柄轴按死区与重复节奏转成方向导航；Submit/Cancel 取按下边沿。
/// </summary>
public class StandardUIInputModule : UIInputModule
{
    /// <summary>手柄轴死区。</summary>
    public const float GamepadDeadZone = 0.25f;

    /// <summary>首次方向重复的延迟，秒。</summary>
    public const float NavigationFirstRepeat = 0.4f;

    /// <summary>后续方向重复的间隔，秒。</summary>
    public const float NavigationRepeatInterval = 0.1f;

    private UINavigation? heldNavigation;
    private float heldTime;
    private float repeatTimer;
    //轴在每个方向上的按下边沿：值过死区才算一次新按下。
    private bool axisUp;
    private bool axisDown;
    private bool axisLeft;
    private bool axisRight;

    /// <summary>处理一批原始事件。</summary>
    public override void Process(ReadOnlySpan<UIRawInputEvent> events, float deltaTime, UIInputRouter router)
    {
        ArgumentNullException.ThrowIfNull(router);

        for (int index = 0; index < events.Length; ++index)
        {
            UIRawInputEvent raw = events[index];
            switch (raw.Kind)
            {
            case UIInputKind.PointerMove:
                router.ProcessPointer(ToPointer(raw, UIPointerPhase.Move));
                break;
            case UIInputKind.PointerDown:
                router.ProcessPointer(ToPointer(raw, UIPointerPhase.Down));
                break;
            case UIInputKind.PointerUp:
                router.ProcessPointer(ToPointer(raw, UIPointerPhase.Up));
                break;
            case UIInputKind.PointerCancel:
            case UIInputKind.WindowFocusLost:
                //失焦与取消：立刻放开全部指针与捕获。
                if (raw.Kind == UIInputKind.WindowFocusLost) router.Reset();
                else router.ProcessPointer(ToPointer(raw, UIPointerPhase.Cancel));
                break;
            case UIInputKind.Wheel:
                router.ProcessScroll(ToPointer(raw, UIPointerPhase.Scroll));
                break;
            case UIInputKind.KeyDown:
                //输入框有焦点时按键先归它，处理掉就不再走导航。
                if (ProcessTextKey(raw, router)) break;
                ProcessKeyDown(raw, router);
                break;
            case UIInputKind.KeyUp:
                ProcessKeyUp(raw, router);
                break;
            case UIInputKind.GamepadState:
                ProcessGamepad(raw, router);
                break;
            case UIInputKind.CompositionStart:
            case UIInputKind.CompositionUpdate:
            case UIInputKind.CompositionCommit:
            case UIInputKind.CompositionCancel:
                ProcessComposition(raw, router);
                break;
            case UIInputKind.TextCommit:
                ProcessTextCommit(raw, router);
                break;
            default:
                break;
            }
        }

        TickNavigation(deltaTime, router);
    }

    //输入框有焦点时接管编辑类按键；返回真表示这条事件已经被吃掉。
    private static bool ProcessTextKey(in UIRawInputEvent raw, UIInputRouter router)
    {
        TextField? field = UIWorldContext.Current?.TextFocus;
        if (field == null || !field.CanInteract()) return false;

        bool shift = (raw.data.modifiers & (uint)UIInputModifiers.Shift) != 0;
        bool control = (raw.data.modifiers & (uint)UIInputModifiers.Control) != 0;
        bool handled = true;
        switch ((KeyEnum)raw.data.key)
        {
        case KeyEnum.LEFT: field.MoveCaret(-1, shift); break;
        case KeyEnum.RIGHT: field.MoveCaret(1, shift); break;
        case KeyEnum.HOME: field.SetSelection(field.GetCaret(), field.GetCaret()); field.MoveCaret(-int.MaxValue / 2, shift); break;
        case KeyEnum.END: field.MoveCaret(int.MaxValue / 2, shift); break;
        case KeyEnum.BACKSPACE: field.DeleteBackward(); break;
        case KeyEnum.DEL: field.DeleteForward(); break;
        case KeyEnum.ENTER: field.RaiseSubmitted(); break;
        case KeyEnum.A when control: field.SelectAll(); break;
        case KeyEnum.C when control: field.CopySelection(); break;
        case KeyEnum.X when control: field.CutSelection(); break;
        case KeyEnum.V when control: field.Paste(); break;
        default: handled = false; break;
        }

        //被输入框吃掉的按下要确认消费，游戏与导航都不该再看到它。
        if (handled) router.ConsumeSequence(raw.data.sequence);
        return handled;
    }

    //组合事件：按会话令牌驱动输入框的组合状态；令牌不符的一律忽略。
    private static void ProcessComposition(in UIRawInputEvent raw, UIInputRouter router)
    {
        TextField? field = UIWorldContext.Current?.TextFocus;
        if (field == null) return;
        router.ConsumeSequence(raw.data.sequence);

        switch (raw.Kind)
        {
        case UIInputKind.CompositionStart:
            field.BeginComposition(raw.data.sequence);
            break;
        case UIInputKind.CompositionUpdate:
            field.UpdateComposition(raw.data.sequence, raw.text, (int)raw.data.caretScalar);
            break;
        case UIInputKind.CompositionCommit:
            field.CommitComposition(raw.data.sequence, raw.text);
            break;
        case UIInputKind.CompositionCancel:
            field.CancelComposition();
            break;
        default:
            break;
        }
    }

    //普通字符输入：组合期间不直接落文本，交给组合提交。
    private static void ProcessTextCommit(in UIRawInputEvent raw, UIInputRouter router)
    {
        TextField? field = UIWorldContext.Current?.TextFocus;
        if (field == null || raw.text.Length == 0) return;
        router.ConsumeSequence(raw.data.sequence);
        if (field.HasComposition()) return;
        field.ReplaceSelection(raw.text);
    }

    /// <summary>清空瞬时状态。</summary>
    public override void Reset()
    {
        heldNavigation = null;
        heldTime = 0.0f;
        repeatTimer = 0.0f;
        axisUp = axisDown = axisLeft = axisRight = false;
    }

    //按下边沿触发一次导航；持续按住时先等首延迟、再按固定间隔重复。
    private void ProcessKeyDown(in UIRawInputEvent raw, UIInputRouter router)
    {
        UINavigation? direction = DirectionOfKey((KeyEnum)raw.data.key);
        if (direction == null) return;

        if (heldNavigation == direction.Value) return;
        heldNavigation = direction;
        heldTime = 0.0f;
        repeatTimer = 0.0f;
        router.ProcessNavigation(direction.Value);
    }

    private void ProcessKeyUp(in UIRawInputEvent raw, UIInputRouter router)
    {
        UINavigation? direction = DirectionOfKey((KeyEnum)raw.data.key);
        if (direction == null || heldNavigation != direction.Value) return;
        heldNavigation = null;
        heldTime = 0.0f;
        repeatTimer = 0.0f;
    }

    //时间推进：按住时按节奏重复；松开就停。
    private void TickNavigation(float deltaTime, UIInputRouter router)
    {
        if (heldNavigation == null) return;
        if (!float.IsFinite(deltaTime) || deltaTime <= 0.0f) return;

        heldTime += deltaTime;
        if (heldTime < NavigationFirstRepeat) return;

        repeatTimer += deltaTime;
        if (repeatTimer < NavigationRepeatInterval) return;
        repeatTimer -= NavigationRepeatInterval;
        router.ProcessNavigation(heldNavigation.Value);
    }

    //手柄状态：轴过死区即当作方向按下，回到死区即释放；Submit/Cancel 用按下边沿。
    private void ProcessGamepad(in UIRawInputEvent raw, UIInputRouter router)
    {
        uint key = raw.data.key;
        float value = raw.data.value;
        if (key == (uint)UIGamepadKey.AxisX)
        {
            bool left = value < -GamepadDeadZone;
            bool right = value > GamepadDeadZone;
            if (right && !axisRight) PressGamepad(router, UINavigation.Right);
            if (left && !axisLeft) PressGamepad(router, UINavigation.Left);
            if (!right && !left && (axisRight || axisLeft)) heldNavigation = null;
            axisRight = right;
            axisLeft = left;
        }
        else if (key == (uint)UIGamepadKey.AxisY)
        {
            bool up = value > GamepadDeadZone;
            bool down = value < -GamepadDeadZone;
            if (up && !axisUp) PressGamepad(router, UINavigation.Up);
            if (down && !axisDown) PressGamepad(router, UINavigation.Down);
            if (!up && !down && (axisUp || axisDown)) heldNavigation = null;
            axisUp = up;
            axisDown = down;
        }
        else if (key == (uint)UIGamepadKey.Submit)
        {
            if (value > 0.5f) router.ProcessSubmit();
        }
        else if (key == (uint)UIGamepadKey.Cancel)
        {
            if (value > 0.5f) router.ProcessCancel();
        }
    }

    private void PressGamepad(UIInputRouter router, UINavigation direction)
    {
        heldNavigation = direction;
        heldTime = 0.0f;
        repeatTimer = 0.0f;
        router.ProcessNavigation(direction);
    }

    private static UINavigation? DirectionOfKey(KeyEnum key) => key switch
    {
        KeyEnum.UP => UINavigation.Up,
        KeyEnum.DOWN => UINavigation.Down,
        KeyEnum.LEFT => UINavigation.Left,
        KeyEnum.RIGHT => UINavigation.Right,
        _ => null,
    };

    private static UIPointerEvent ToPointer(in UIRawInputEvent raw, UIPointerPhase phase) => new()
    {
        pointerId = raw.data.pointerId,
        phase = phase,
        button = raw.data.key,
        position = raw.data.position,
        delta = raw.data.delta,
        timestamp = raw.data.timestamp,
        sequence = raw.data.sequence,
    };
}
