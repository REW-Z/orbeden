using Orbeden;

namespace OrbedenEditor;

/// <summary>渲染与显示参数的集中入口，按归属分成两节：
/// 世界级的环境与天空盒存在当前 World 文件里，项目级的曝光存在 ProjectSettings.display 里。</summary>
internal sealed class RenderingPanel : EditorPanel
{
    private string root = "\0";
    private bool dirty;
    private string status = string.Empty;
    private float exposure = EditorDisplaySettings.DefaultExposure;

    public override EditorPanelInfo Info => new("rendering", "Rendering", false,
        new vector2(520, 460), PanelDockPlacement.Floating, 0.35f, 140);

    /// <summary>关闭项目时把两节的未提交改动都落盘。</summary>
    public override bool SavePendingChanges()
    {
        if (root != PathDefines.ContentRoot) return false;
        if (!EditorEnvironmentSettings.Apply()) return false;
        if (!dirty) return true;
        if (!ApplyDisplay()) return false;
        status = "Rendering settings saved.";
        return true;
    }

    /// <summary>从项目设置重新读取项目级草稿。</summary>
    private void ReloadDisplay()
    {
        root = PathDefines.ContentRoot;
        EditorDisplaySettings.Refresh();
        exposure = EditorDisplaySettings.Exposure;
        dirty = false;
        status = string.Empty;
    }

    /// <summary>把项目级草稿写回 ProjectSettings.display。</summary>
    private bool ApplyDisplay()
    {
        if (!EditorAssetsNative.CanModifyAssets()) { status = "Rendering settings cannot be changed while playing."; return false; }
        if (!EditorDisplaySettings.Save(exposure, out string error)) { status = error; return false; }
        dirty = false;
        return true;
    }

    /// <summary>绘制两节参数，改动累积在各自草稿里直到 Apply。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        if (string.IsNullOrWhiteSpace(PathDefines.ContentRoot)) { EditorGUI.Label("No project loaded."); return; }
        if (root != PathDefines.ContentRoot) ReloadDisplay();

        bool unsaved = dirty || EditorEnvironmentSettings.HasPendingChanges;
        EditorGUI.Label(unsaved ? "Rendering (Unsaved)" : "Rendering");

        //环境设置同时能在资源 Inspector 里改，两处共用同一份实现，见 EditorEnvironmentSettings
        int environment = NativeEditorGUI.TreeNode("World Environment##rendering_environment", false, false, true);
        if ((environment & 1) != 0)
        {
            try
            {
                EditorGUI.Label("Stored in the current world file. Also editable from the world asset inspector.");
                EditorEnvironmentSettings.Draw("rendering_panel");
            }
            finally { NativeEditorGUI.TreePop(); }
        }

        int display = NativeEditorGUI.TreeNode("Project Display##rendering_display", false, false, true);
        if ((display & 1) != 0)
        {
            try
            {
                EditorGUI.BeginDisabled(!EditorAssetsNative.CanModifyAssets());
                try
                {
                    EditorGUI.Label("Project-level settings, stored in ProjectSettings.display.");
                    if (EditorGUI.InputFloat("Exposure", ref exposure)) dirty = true;
                    EditorGUI.Label("Exposure is how the scene is observed, not how bright its lights are.");
                    EditorGUI.Label("A Camera component can override it per camera.");
                    EditorGUI.Separator();
                    if (EditorGUI.Button(dirty ? "Apply *" : "Apply")) ApplyDisplay();
                    EditorGUI.SameLine();
                    if (EditorGUI.Button("Revert")) ReloadDisplay();
                    if (status.Length != 0) EditorGUI.Label(status);
                }
                finally { EditorGUI.EndDisabled(); }
            }
            finally { NativeEditorGUI.TreePop(); }
        }
    }
}
