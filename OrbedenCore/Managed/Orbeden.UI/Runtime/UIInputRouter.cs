using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Orbeden;

/// <summary>
/// 输入路由器。指针、滚轮与导航都从这里进入控件：它自己维护悬停、按下、捕获与焦点，
/// 控件只收到已经判定过归属的回调。命中一律基于最近一次成功呈现的快照。
/// </summary>
public sealed class UIInputRouter
{
    //超过这个位移才算拖动；手指抖动不该变成拖动。
    public const float DragThreshold = 6.0f;

    private readonly UIWorldContext context;
    private readonly UIRaycaster raycaster;
    private readonly Dictionary<uint, UIPointerState> pointers = [];
    //节点上的输入处理器；与命中过滤共用上下文里那一份。
    private UIHandlerCache handlers => context.HandlerCache;
    //一次派发里的处理器列表；复用同一份缓冲，不逐事件分配。
    private readonly List<object> handlerScratch = [];
    private readonly List<UIControl> navigationOrder = [];
    private readonly List<UIControl> candidates = [];
    //本阶段被 UI 消费的事件序列；输入阶段结束时一次性确认给原生侧。
    private readonly List<ulong> consumed = [];

    //指针当前悬停的处理节点（屏幕命中）；触摸不产生悬停。
    private UIEventTarget? hoveredNode;
    private UIControl? focus;
    //已经被取消的按下序列：Up 不得把它恢复成一次点击。
    private readonly HashSet<ulong> cancelledSequences = [];
    //取消某个对象时复用的指针列表，避免遍历中改表。
    private readonly List<uint> objectCancelScratch = [];

    internal UIInputRouter(UIWorldContext context, UIRaycaster raycaster)
    {
        this.context = context;
        this.raycaster = raycaster;
    }

    /// <summary>本阶段被消费的事件序列。</summary>
    public IReadOnlyList<ulong> ConsumedSequences => consumed;

    /// <summary>当前持有焦点的控件。</summary>
    public UIControl? FocusedControl => focus;

    /// <summary>指针的当前状态；没有记录时返回默认值。</summary>
    public UIPointerState GetPointerState(uint pointerId) =>
        pointers.TryGetValue(pointerId, out UIPointerState state) ? state : default;

    /// <summary>开始一个输入阶段：清空上一阶段的消费记录与深度缓存。</summary>
    public void BeginPhase()
    {
        consumed.Clear();
        raycaster.ResetDepthCache();
        //取消标记只在一个阶段内有效：跨阶段的 Up 不该被上一阶段的取消影响。
        cancelledSequences.Clear();
    }

    /// <summary>结束输入阶段：把消费记录交给原生侧，被消费的按下会占有对应键。</summary>
    public void EndPhase()
    {
        if (consumed.Count == 0) return;
        context.NativeBridge?.ConsumeInput(CollectionsMarshal.AsSpan(consumed));
    }

    //命中之后从图形所在节点向祖先找这一层接口的实现者：第一个有实现者的节点就是处理节点。
    private bool TryCollectHandlers<T>(UINode? start, out UIEventTarget target)
    {
        handlerScratch.Clear();
        for (UINode? current = start; current != null; current = current.Parent)
        {
            UIControl? nodeControl = null;
            handlerScratch.Clear();
            foreach (Script script in handlers.GetHandlers(current.Ens))
            {
                if (!script.GetEnabled()) continue;
                if (script is UIControl control)
                {
                    //不能交互的控件跳过自己，但不影响同节点上的普通处理器。
                    if (!control.CanInteract()) continue;
                    nodeControl = control;
                }
                if (script is T) handlerScratch.Add(script);
            }
            if (handlerScratch.Count == 0) continue;

            target = new UIEventTarget(current.Ens, nodeControl);
            return true;
        }

        target = default;
        return false;
    }

    //把本层收集到的处理器按顺序调用一遍；回调里销毁的组件在调用前跳过。
    private void InvokeHandlers<T>(in UIPointerEvent input, Action<T, UIPointerEvent> invoke)
    {
        foreach (object script in handlerScratch)
        {
            if (script is not T handler) continue;
            if (script is Script alive && (!alive.IsAlive || !alive.GetEnabled())) continue;
            invoke(handler, input);
        }
    }

    //命中图形所在节点；命中没有图形时返回空。
    private UINode? FindHitNode(in UIHitResult result) =>
        result.visual != null ? context.FindNode(result.visual.EnsId) : null;

    //把事件发给已记录的处理节点上的接口实现者；节点没了就什么都不发。
    private void InvokeOnNode<T>(in UIEventTarget target, in UIPointerEvent input,
        Action<T, UIPointerEvent> invoke)
    {
        if (!target.IsValid) return;
        UINode? node = context.FindNode(target.ens);
        if (node == null) return;
        TryCollectHandlers<T>(node, out _);
        InvokeHandlers(input, invoke);
    }

    /// <summary>处理一次指针事件。</summary>
    public void ProcessPointer(in UIPointerEvent input)
    {
        switch (input.phase)
        {
        case UIPointerPhase.Scroll:
            ProcessScroll(input);
            return;
        case UIPointerPhase.Cancel:
            CancelPointer(input.pointerId, input);
            return;
        default:
            break;
        }

        pointers.TryGetValue(input.pointerId, out UIPointerState state);
        state.lastPosition = input.position;

        bool hit = raycaster.Raycast(input, out UIHitResult result);
        UINode? hitNode = hit ? FindHitNode(result) : null;

        if (input.phase == UIPointerPhase.Down)
        {
            state.active = true;
            state.dragging = false;
            state.pressPosition = input.position;
            state.button = input.button;

            bool handled = TryCollectHandlers<IUIPointerDownHandler>(hitNode, out UIEventTarget target);
            state.downTarget = handled ? target : default;
            //有图形挡住就算 UI 消费了这次按下，即使它身上没有处理者。
            if (hit || handled)
            {
                state.consumed = true;
                Consume(input.sequence);
            }

            if (handled)
            {
                if (target.control != null) target.control.Pressed = true;
                Capture(input.pointerId, target);
                InvokeHandlers<IUIPointerDownHandler>(input, static (handler, payload) => handler.OnPointerDown(payload));
                //回调可能销毁了节点，回写状态前重查一次身份。
                if (!target.IsAlive) pointers.Remove(input.pointerId);
            }
        }
        else if (input.phase == UIPointerPhase.Move)
        {
            UpdateHover(input, hitNode);

            if (state.active && state.captureTarget.IsValid)
            {
                if (!state.dragging
                    && Distance(input.position, state.pressPosition) > DragThreshold)
                {
                    state.dragging = true;
                }
                //拖出目标就不再算按住：释放时不会变成点击。
                if (!state.dragging && hitNode == null || hitNode.Ens.id != state.captureTarget.ens.id)
                {
                    if (state.captureTarget.control != null) state.captureTarget.control.Pressed = false;
                }
                InvokeOnNode<IUIPointerMoveHandler>(state.captureTarget, input,
                    static (handler, payload) => handler.OnPointerMove(payload));
            }
            else if (TryCollectHandlers<IUIPointerMoveHandler>(hitNode, out _))
            {
                InvokeHandlers<IUIPointerMoveHandler>(input, static (handler, payload) => handler.OnPointerMove(payload));
            }
        }
        else if (input.phase == UIPointerPhase.Up)
        {
            UIEventTarget captured = state.captureTarget;
            bool cancelled = cancelledSequences.Contains(state.pressSequence);

            //离开目标后释放不构成点击：只有没拖动、没被取消、且仍在原目标上才算。
            //按下状态在离开目标或拖动时就已清掉，控件据此自行决定算不算点击。
            if (captured.IsValid && !cancelled)
            {
                InvokeOnNode<IUIPointerUpHandler>(captured, input,
                    static (handler, payload) => handler.OnPointerUp(payload));
            }

            if (captured.IsValid)
            {
                if (captured.control != null) captured.control.Pressed = false;
                ReleaseCapture(input.pointerId, cancel: false);
            }

            state.active = false;
            state.dragging = false;
            state.downTarget = default;
            state.consumed = false;
        }

        if (input.phase == UIPointerPhase.Down) state.pressSequence = input.sequence;
        pointers[input.pointerId] = state;
    }

    /// <summary>滚轮：从命中节点向祖先传播，没人处理就交最近的可滚动容器。</summary>
    public void ProcessScroll(in UIPointerEvent input)
    {
        if (!raycaster.Raycast(input, out UIHitResult result)) return;
        Consume(input.sequence);

        //从命中节点向祖先找滚轮处理者；同一节点按组件顺序，直到有人消费。
        for (UINode? current = FindHitNode(result); current != null; current = current.Parent)
        {
            foreach (Script script in handlers.GetHandlers(current.Ens))
            {
                if (!script.GetEnabled()) continue;
                if (script is UIControl control && !control.CanInteract()) continue;
                if (script is not IUIScrollHandler scroll) continue;
                if (scroll.OnScroll(input.delta)) return;
            }
        }
    }

    /// <summary>
    /// 把指针捕获交给控件。同一个指针只能有一个捕获者；同一个控件也只接受一个拖动指针，
    /// 后来者接手时先取消原来的那个。
    /// </summary>
    public void Capture(uint pointerId, in UIEventTarget target)
    {
        if (!target.IsValid) return;

        pointers.TryGetValue(pointerId, out UIPointerState state);
        if (state.captureTarget.IsValid && state.captureTarget.ens.id != target.ens.id)
        {
            //移交先取消原目标，再建立新捕获。
            CancelTarget(state.captureTarget, pointerId);
        }

        //同一节点只接受一个拖动指针：把别的指针从它身上摘掉。
        foreach (KeyValuePair<uint, UIPointerState> entry in pointers)
        {
            if (entry.Key == pointerId) continue;
            if (entry.Value.captureTarget.ens.id != target.ens.id) continue;
            UIPointerState other = entry.Value;
            CancelTarget(target, entry.Key);
            other.captureTarget = default;
            pointers[entry.Key] = other;
        }

        state.captureTarget = target;
        pointers[pointerId] = state;
    }

    /// <summary>释放捕获；cancel 为真时先给控件一次取消通知。</summary>
    public void ReleaseCapture(uint pointerId, bool cancel)
    {
        if (!pointers.TryGetValue(pointerId, out UIPointerState state)) return;
        if (state.captureTarget.IsValid && cancel) CancelTarget(state.captureTarget, pointerId);
        state.captureTarget = default;
        pointers[pointerId] = state;
    }

    /// <summary>把一条事件序列标记为已消费；弹层关闭时要消费掉那次外部点击。</summary>
    public void ConsumeSequence(ulong sequence) => Consume(sequence);

    /// <summary>取消一路指针：捕获、按下与悬停全部清掉，Up 不得恢复这次点击。</summary>
    public void CancelPointer(uint pointerId, in UIPointerEvent input)
    {
        if (!pointers.TryGetValue(pointerId, out UIPointerState state)) return;
        if (state.captureTarget.IsValid)
        {
            InvokeOnNode<IUIPointerCancelHandler>(state.captureTarget, input,
                static (handler, payload) => handler.OnPointerCancel(payload));
        }
        if (state.downTarget.control != null) state.downTarget.control.Pressed = false;

        //记下这次按下的序列：即使随后收到 Up，也不再当成点击。
        if (state.active) cancelledSequences.Add(state.pressSequence);

        state.active = false;
        state.dragging = false;
        state.captureTarget = default;
        state.downTarget = default;
        state.consumed = false;
        pointers[pointerId] = state;
    }

    /// <summary>设置焦点；传空表示清除焦点。</summary>
    public void SetFocus(UIControl? control)
    {
        if (control != null && !control.CanInteract()) return;
        if (ReferenceEquals(focus, control)) return;

        UIControl? previous = focus;
        focus = control;
        previous?.Focused = false;
        control?.Focused = true;
    }

    /// <summary>取消与某个节点有关的交互状态；节点上的组件被禁用或销毁时调用。</summary>
    public void CancelNode(EnsId ens)
    {
        if (ens.IsNull) return;

        //先收集再取消：取消会改写指针表，不能在遍历中动它。
        objectCancelScratch.Clear();
        foreach (KeyValuePair<uint, UIPointerState> entry in pointers)
        {
            UIEventTarget target = entry.Value.captureTarget.IsValid
                ? entry.Value.captureTarget
                : entry.Value.downTarget;
            if (!target.IsValid || target.ens.id != ens.id) continue;
            objectCancelScratch.Add(entry.Key);
        }
        foreach (uint pointerId in objectCancelScratch) CancelPointer(pointerId, default);

        if (hoveredNode is UIEventTarget hovered && hovered.ens.id == ens.id) hoveredNode = null;
        if (focus != null && focus.EnsId.id == ens.id) SetFocus(null);
    }

    /// <summary>清空全部交互状态；世界切换或输入模块被替换时调用。</summary>
    public void Reset()
    {
        foreach (KeyValuePair<uint, UIPointerState> entry in pointers)
        {
            UIEventTarget target = entry.Value.captureTarget.IsValid
                ? entry.Value.captureTarget
                : entry.Value.downTarget;
            if (!target.IsValid) continue;
            if (target.control != null) target.control.Pressed = false;
            InvokeOnNode<IUIPointerCancelHandler>(target, default,
                static (handler, payload) => handler.OnPointerCancel(payload));
        }
        pointers.Clear();
        cancelledSequences.Clear();
        consumed.Clear();
        hoveredNode = null;
        SetFocus(null);
    }

    /// <summary>
    /// 方向导航。先看显式引用；没有就用几何候选：位于方向半平面内，
    /// score = 前向距离 + 2×垂直距离，最小者获焦点，距离相同按绘制顺序。
    /// </summary>
    public bool ProcessNavigation(UINavigation direction)
    {
        if (focus == null) return false;
        //控件自己能处理就不移动焦点。
        if (((IUINavigationHandler)focus).OnNavigate(direction)) return true;

        UIControl? explicitTarget = focus.GetNavigation(direction);
        if (explicitTarget != null && explicitTarget.CanInteract())
        {
            SetFocus(explicitTarget);
            return true;
        }

        CollectNavigationOrder();
        if (!TryFindCandidate(direction, out UIControl? candidate)) return false;
        SetFocus(candidate);
        return true;
    }

    /// <summary>提交：交给当前焦点控件。</summary>
    public void ProcessSubmit()
    {
        if (focus != null && focus.CanInteract()) ((IUISubmitHandler)focus).OnSubmit();
    }

    /// <summary>取消：交给当前焦点控件。</summary>
    public void ProcessCancel()
    {
        if (focus != null) ((IUICancelHandler)focus).OnCancel();
    }

    //刷新悬停：触摸不产生悬停；离开的控件收到退出通知。
    //刷新悬停：触摸不产生悬停；换节点时先给旧节点发退出，再给新节点发进入。
    private void UpdateHover(in UIPointerEvent input, UINode? hitNode)
    {
        if (input.pointerId != 0) return;

        bool hasEnter = TryCollectHandlers<IUIPointerEnterHandler>(hitNode, out UIEventTarget entered);
        UIEventTarget next = hasEnter ? entered : default;
        if (hoveredNode.HasValue && hoveredNode.Value.ens.id == next.ens.id && !hasEnter == !hoveredNode.HasValue) return;

        if (hoveredNode.HasValue)
        {
            if (hoveredNode.Value.control != null) hoveredNode.Value.control.Hovered = false;
            InvokeOnNode<IUIPointerExitHandler>(hoveredNode.Value, input,
                static (handler, payload) => handler.OnPointerExit(payload));
        }

        hoveredNode = hasEnter ? next : null;
        if (!hasEnter) return;

        next.control?.Hovered = true;
        InvokeHandlers<IUIPointerEnterHandler>(input,
            static (handler, payload) => handler.OnPointerEnter(payload));
    }

    //取消一路指针在某个处理节点上的交互。
    private void CancelTarget(in UIEventTarget target, uint pointerId)
    {
        if (!pointers.TryGetValue(pointerId, out UIPointerState state)) return;
        UIPointerEvent input = default;
        input.pointerId = pointerId;
        input.phase = UIPointerPhase.Cancel;
        input.position = state.lastPosition;
        InvokeOnNode<IUIPointerCancelHandler>(target, input,
            static (handler, payload) => handler.OnPointerCancel(payload));
        if (target.control != null) target.control.Pressed = false;
    }

    //导航候选按绘制顺序收集：先普通层再弹层，父控件先于子控件。
    private void CollectNavigationOrder()
    {
        navigationOrder.Clear();
        foreach (Canvas canvas in context.Canvases)
        {
            UINode? root = context.FindNode(canvas.EnsId);
            if (root == null) continue;
            for (int layer = 0; layer <= 1; ++layer) CollectNavigationOrder(root, layer);
        }
    }

    private void CollectNavigationOrder(UINode node, int layer)
    {
        if (node.Layer == layer && node.GetElement<UIControl>() is UIControl control) navigationOrder.Add(control);
        foreach (UINode child in node.Children) CollectNavigationOrder(child, layer);
    }

    //半平面内的候选按 score 取最小；距离相同保留先出现的那个（绘制顺序在前）。
    private bool TryFindCandidate(UINavigation direction, out UIControl candidate)
    {
        candidate = null!;
        if (focus == null) return false;
        if (!TryGetFocusCenter(out vector2 origin)) return false;

        float best = float.MaxValue;
        foreach (UIControl control in navigationOrder)
        {
            if (ReferenceEquals(control, focus) || !control.CanInteract()) continue;
            if (!TryGetCenter(control, out vector2 center)) continue;

            vector2 offset = new(center.x - origin.x, center.y - origin.y);
            float forward = direction switch
            {
                UINavigation.Up => offset.y,
                UINavigation.Down => -offset.y,
                UINavigation.Left => -offset.x,
                _ => offset.x,
            };
            //必须在方向的半平面内，否则会出现“往上却跳到下方控件”。
            if (forward <= 0.0f) continue;

            float perpendicular = direction is UINavigation.Up or UINavigation.Down
                ? MathF.Abs(offset.x)
                : MathF.Abs(offset.y);
            float score = forward + 2.0f * perpendicular;
            if (score >= best) continue;

            best = score;
            candidate = control;
        }
        return candidate != null;
    }

    private bool TryGetFocusCenter(out vector2 center)
    {
        center = default;
        return focus != null && TryGetCenter(focus, out center);
    }

    //控件中心取自身及其祖先在画布空间的中心；取不到返回假。
    private bool TryGetCenter(UIControl control, out vector2 center)
    {
        center = default;
        UINode? node = context.FindNode(control.EnsId);
        if (node?.Layout == null) return false;

        UIRect rect = node.Layout.GetResolvedRect();
        matrix4x4 toCanvas = UIMatrix.Compose(node);
        center = UIMatrix.TransformPoint(toCanvas, rect.Center);
        return true;
    }

    private void Consume(ulong sequence)
    {
        if (sequence != 0) consumed.Add(sequence);
    }

    private static float Distance(vector2 left, vector2 right)
    {
        float x = right.x - left.x;
        float y = right.y - left.y;
        return MathF.Sqrt(x * x + y * y);
    }
}
