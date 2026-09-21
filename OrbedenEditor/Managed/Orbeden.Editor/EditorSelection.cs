using System;

namespace OrbedenEditor;

/// <summary>
/// 编辑器选择：记录当前「谁持有选中项」，Delete / Rename 这类全局命令都从这里取目标。
/// 选中项本身仍归各面板所有（Ens 与资源路径不是同一种东西），选择系统只管所有权与派发。
/// </summary>
internal static class EditorSelection
{
    private static string ownerPanelId = string.Empty;

    /// <summary>面板每帧上报：本帧是否拥有焦点、当前是否有选中项。焦点在别的面板时上报无效。</summary>
    public static void Report(string panelId, bool focused, bool hasSelection)
    {
        if (focused) ownerPanelId = hasSelection ? panelId : string.Empty;
        else if (string.Equals(ownerPanelId, panelId, StringComparison.Ordinal)) ownerPanelId = string.Empty;
    }

    /// <summary>面板不再参与选择（隐藏、切项目）时清掉自己的所有权。</summary>
    public static void Clear(string panelId)
    {
        if (string.Equals(ownerPanelId, panelId, StringComparison.Ordinal)) ownerPanelId = string.Empty;
    }

    /// <summary>当前是否有面板持有可操作的选中项。</summary>
    public static bool HasSelection => ownerPanelId.Length != 0;

    /// <summary>请求重命名当前选中项。</summary>
    public static void Rename() => Dispatch("rename", panel => panel.OnRenameRequested());

    /// <summary>请求删除当前选中项。</summary>
    public static void Delete() => Dispatch("delete", panel => panel.OnDeleteRequested());

    //只派发给持有选择的面板，且它必须还在显示
    private static void Dispatch(string command, Action<EditorPanel> handler)
    {
        //弹窗正挡在界面上，别的命令等它结束
        if (EditorDialog.IsOpen || !HasSelection) return;

        EditorPanel? panel = EditorPanelRegistry.FindVisiblePanel(ownerPanelId);
        if (panel == null) return;

        try
        {
            handler(panel);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Editor {command} failed: {panel.GetType().FullName}: {ex}");
        }
    }
}
