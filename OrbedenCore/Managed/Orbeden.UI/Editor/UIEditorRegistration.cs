using System.Runtime.CompilerServices;
using Orbeden;
using OrbedenEditor;

namespace OrbedenEditor;

/// <summary>
/// UI 编辑器包的入口。模块初始化时只登记创建菜单；控件编辑器由
/// CustomEditorRegistry 扫描本程序集时自动接入，不需要在这里重复登记。
/// 注销随程序集卸载进行，避免把委托与类型留在编辑器侧的静态表里。
/// </summary>
internal static class UIEditorRegistration
{
    /// <summary>程序集模块初始化。</summary>
    [ModuleInitializer]
    internal static void Initialize()
    {
        UICreationMenu.Register();
        FontEditor.Register();
        UIPreviewController.Register();
        EditorSceneExtensions.Register("Orbeden.UI", new UISceneExtension());
        ManagedAssemblySession.RegisterUnloadHandler(() => EditorSceneExtensions.Unregister("Orbeden.UI"));
        //程序集卸载时把提供者摘掉，面板不会继续握着旧程序集的对象。
        ManagedAssemblySession.RegisterUnloadHandler(UIPreviewController.Unregister);
    }
}
