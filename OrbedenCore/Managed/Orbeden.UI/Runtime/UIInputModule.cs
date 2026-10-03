using System;

namespace Orbeden;

/// <summary>
/// 输入模块。把一帧的原始平台事件翻译成 UI 的指针、滚轮与导航动作，
/// 再交给路由器执行；自身不维护交互状态——那是路由器的职责。
/// </summary>
public abstract class UIInputModule
{
    /// <summary>
    /// 处理一批原始事件。events 是本阶段新增的全部事件，deltaTime 是真实帧时间；
    /// 模拟暂停不影响 UI 的时间推进。
    /// </summary>
    public abstract void Process(ReadOnlySpan<UIRawInputEvent> events, float deltaTime, UIInputRouter router);

    /// <summary>清空模块自身的瞬时状态；被替换或世界切换时调用。</summary>
    public virtual void Reset()
    {
    }
}
