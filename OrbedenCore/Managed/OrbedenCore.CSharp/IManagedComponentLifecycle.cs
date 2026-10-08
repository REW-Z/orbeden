namespace Orbeden;

/// <summary>
/// 托管组件的基础生命周期接口，由 ScriptRuntime 直接调用，不按方法名扫描。
/// 引用恢复完成后附着；活动状态只在真实变化时通知；销毁前先收到活动取消再收到分离。
/// </summary>
public interface IManagedComponentLifecycle
{
    /// <summary>序列化前同步运行时配置。</summary>
    void OnComponentBeforeSerialize() { }

    /// <summary>全部宿主包装建立且本实例字段恢复后调用一次。</summary>
    void OnComponentAttached();

    /// <summary>活动条件（enabled 且 Ens.WorldActive）变化时调用，相同状态不重复通知。</summary>
    void OnComponentActiveChanged(bool active);

    /// <summary>宿主字段被外部整体应用后调用一次。</summary>
    void OnComponentFieldsChanged();

    /// <summary>组件分离前调用；已附着实例在此之前会先收到活动取消。</summary>
    void OnComponentDetached();
}
