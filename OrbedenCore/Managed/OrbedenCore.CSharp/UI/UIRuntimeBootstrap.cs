namespace Orbeden;

/// <summary>
/// UI 运行时的接入点。只登记常驻帧系统工厂，不创建任何世界对象；
/// 世界附着时才由 ManagedFrameSystems 实例化上下文。
/// </summary>
internal static class UIRuntimeBootstrap
{
    /// <summary>UI 帧系统在注册表里的固定 id。</summary>
    internal const string FrameSystemId = "Orbeden.RetainedGUI";

    /// <summary>
    /// 接入原生函数表并登记 UI 帧系统工厂。由引擎绑定初始化调用，可重复执行：
    /// 登记必须早于世界附着，且核心程序集的登记不随会话卸载清除。
    /// </summary>
    internal static void Register()
    {
        //版本或尺寸不符时拒绝接入：此时 UI 只做数据与布局，不向原生提交任何东西。
        RetainedGuiNative.Initialize(OrbedenCoreRuntime.RetainedGuiApi);

        if (ManagedFrameSystems.IsRegistered(FrameSystemId)) return;
        //字形缓存的页纹理属于核心侧资源，与程序集同生命周期，不随世界更换重建。
        ManagedFrameSystems.RegisterBuiltin(FrameSystemId, static () => new UIWorldContext());
    }
}
