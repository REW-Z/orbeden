using Orbeden;

namespace OrbedenEditor;

/// <summary>
/// UI 编辑器的接入点。引擎编辑器初始化时登记一次：创建菜单、字体资源检视、场景扩展
/// 与预览提供者都是常驻项，不随游戏程序集卸载。控件编辑器由 CustomEditorRegistry
/// 扫描本程序集时自动接入，不需要在这里重复登记。
/// </summary>
internal static class UIEditorRegistration
{
    /// <summary>登记 UI 的创建菜单、字体资源检视、场景扩展与预览提供者。</summary>
    internal static void Register()
    {
        UICreationMenu.Register();
        FontEditor.Register();
        UIPreviewController.Register();
        EditorSceneExtensions.Register("Orbeden.UI", new UISceneExtension());
    }
}
