using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 事件派发器。事件统一进 FIFO：每个事件先让源更新自己的状态与视觉，
/// 再按持久化行顺序、最后按代码订阅顺序调用监听器。
/// 回调里产生的新事件排到队尾，绝不递归派发；监听器在开始派发时快照，
/// 期间增删订阅从下一条事件起生效。
/// </summary>
public sealed class UIEventDispatcher
{
    /// <summary>单帧派发上限；超出后清空余项并报错，避免回调互相触发把帧拖死。</summary>
    public const int MaxEventsPerFrame = 4096;

    //一条代码订阅。
    private readonly record struct Subscription(IUIEventSource Source, int EventId, Action<UIEvent> Handler);

    private readonly List<UIEvent> queue = [];
    private readonly List<Subscription> subscriptions = [];
    //派发时用的订阅快照；复制之后回调里的增删不影响这一条事件。
    private readonly List<Subscription> snapshot = [];
    private ulong nextSequence = 1;
    private int dispatchedThisFrame;

    /// <summary>队列里还没有派发的事件数。</summary>
    public int PendingCount => queue.Count;

    /// <summary>本帧已经派发的事件数。</summary>
    public int DispatchedThisFrame => dispatchedThisFrame;

    /// <summary>一帧开始时调用：重置本帧计数。</summary>
    public void BeginFrame() => dispatchedThisFrame = 0;

    /// <summary>把一条事件排到队尾。</summary>
    public void Enqueue(in UIEvent value)
    {
        UIEvent entry = value;
        if (entry.sequence == 0)
        {
            entry = new UIEvent(nextSequence, value.source, value.payload, value.eventId, value.target);
        }
        if (nextSequence == ulong.MaxValue) nextSequence = 1;
        else ++nextSequence;
        queue.Add(entry);
    }

    /// <summary>按契约入队一条控件事件。</summary>
    public void Raise(IUIEventSource source, int eventId, in UIEventPayload payload, in EnsId target)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (!source.IsAlive) return;
        Enqueue(new UIEvent(0, source, payload, eventId, target));
    }

    /// <summary>登记一条代码订阅；重复登记同一条无副作用。</summary>
    public void Subscribe(IUIEventSource source, int eventId, Action<UIEvent> handler)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(handler);
        foreach (Subscription existing in subscriptions)
        {
            if (existing.Source == source && existing.EventId == eventId && existing.Handler == handler) return;
        }
        //引用比较：UIControl 的默认相等语义可能被包装类型改写。
        subscriptions.Add(new Subscription(source, eventId, handler));
    }

    /// <summary>注销一条代码订阅。</summary>
    public void Unsubscribe(IUIEventSource source, int eventId, Action<UIEvent> handler)
    {
        for (int index = 0; index < subscriptions.Count; ++index)
        {
            Subscription existing = subscriptions[index];
            if (!ReferenceEquals(existing.Source, source) || existing.EventId != eventId) continue;
            if (existing.Handler != handler) continue;
            subscriptions.RemoveAt(index);
            return;
        }
    }

    /// <summary>注销某个源上的全部订阅；控件销毁时调用。</summary>
    public void ClearSubscriptions(IUIEventSource source)
    {
        for (int index = subscriptions.Count - 1; index >= 0; --index)
        {
            if (ReferenceEquals(subscriptions[index].Source, source)) subscriptions.RemoveAt(index);
        }
    }

    /// <summary>清空队列与全部订阅；世界切换时调用。</summary>
    public void Reset()
    {
        foreach (UIEvent entry in queue) entry.source.OnEventDiscarded(entry);
        queue.Clear();
        subscriptions.Clear();
        snapshot.Clear();
        dispatchedThisFrame = 0;
    }

    /// <summary>
    /// 排空队列。每派发一条就重新检查上限：回调产生的新事件同样计入本帧预算。
    /// </summary>
    public void Dispatch()
    {
        while (queue.Count > 0)
        {
            if (dispatchedThisFrame >= MaxEventsPerFrame)
            {
                //超限：丢弃余项并报错，正在排队的回调结果不再生效。
                Console.Error.WriteLine(
                    $"UIEventDispatcher: 单帧事件超过 {MaxEventsPerFrame} 条，余下的 {queue.Count} 条被丢弃。");
                foreach (UIEvent entry in queue) entry.source.OnEventDiscarded(entry);
                queue.Clear();
                return;
            }

            UIEvent current = queue[0];
            queue.RemoveAt(0);
            ++dispatchedThisFrame;
            DispatchOne(current);
        }
    }

    //派发一条：源自己先更新，再走持久化绑定，最后走代码订阅。
    private void DispatchOne(in UIEvent entry)
    {
        if (!entry.HasValidSource())
        {
            //源已经被销毁或换过代次：这条事件作废，不再惊动任何监听器。
            return;
        }

        //先更新状态/视觉并走持久化行顺序：这两步都在控件内部按顺序完成。
        entry.source.OnEvent(entry);

        //持久化行顺序由控件持有；代码订阅在派发前快照。
        snapshot.Clear();
        snapshot.AddRange(subscriptions);

        foreach (Subscription subscription in snapshot)
        {
            //删除源会终止该源余下的调用。
            if (!entry.HasValidSource()) return;
            if (!ReferenceEquals(subscription.Source, entry.source)) continue;
            if (subscription.EventId != entry.eventId) continue;
            Invoke(subscription.Handler, entry);
        }
    }

    //单个监听器的异常不阻断其他监听器。
    private static void Invoke(Action<UIEvent> handler, in UIEvent entry)
    {
        try
        {
            handler(entry);
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"UIEventDispatcher: 监听器抛出异常。{exception}");
        }
    }
}
