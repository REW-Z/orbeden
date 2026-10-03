using System.Runtime.CompilerServices;

namespace Orbeden;

/// <summary>
/// UI 包的运行期入口。模块初始化时只登记帧系统工厂，不创建任何世界对象；
/// 世界附着时才由 ManagedFrameSystems 实例化上下文。
/// </summary>
internal static class UIRuntimeBootstrap
{
    /// <summary>UI 帧系统在注册表里的固定 id。</summary>
    internal const string FrameSystemId = "Orbeden.RetainedGUI";

    /// <summary>程序集模块初始化：接入原生函数表并登记 UI 帧系统工厂。</summary>
    [ModuleInitializer]
    internal static void Initialize()
    {
        //版本或尺寸不符时拒绝接入：此时 UI 只做数据与布局，不向原生提交任何东西。
        RetainedGuiNative.Initialize(OrbedenCoreRuntime.RetainedGuiApi);

        if (ManagedFrameSystems.IsRegistered(FrameSystemId)) return;
        ManagedFrameSystems.Register(FrameSystemId, static () => new UIWorldContext());
        //字形缓存持有 Atlas 纹理，程序集卸载时必须显式放掉这些资源根。
        ManagedAssemblySession.RegisterUnloadHandler(static () => FontAtlasCache.Shared.Dispose());
    }
}
