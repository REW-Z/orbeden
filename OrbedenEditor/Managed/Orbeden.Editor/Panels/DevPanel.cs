using Orbeden;

namespace OrbedenEditor;

/// <summary>管理项目示例与引擎模板之间的同步。</summary>
internal sealed class DevPanel : EditorPanel
{
    private string report = string.Empty;

    public override EditorPanelInfo Info => new("dev", "Dev", false,
        new vector2(520, 280), PanelDockPlacement.Floating, 0.25f, 120);

    /// <summary>绘制示例目录和同步操作。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        string content = EditorApplication.GetProjectText(EditorProjectField.Content);
        if (EditorApplication.GetProjectText(EditorProjectField.Root).Length == 0)
        {
            EditorGUI.Label("No project loaded.");
            return;
        }
        string templates = EditorApplication.GetProjectText(EditorProjectField.SourceTemplate);
        string projectExamples = Path.Combine(content, "Examples");
        string templateExamples = templates.Length == 0 ? string.Empty : Path.Combine(templates, "Examples");
        EditorGUI.Label("Project examples: " + projectExamples);
        EditorGUI.Label("Repository root: " + EditorApplication.GetProjectText(EditorProjectField.Repository));
        EditorGUI.Label("Template examples: " + (templateExamples.Length == 0 ? "(not found)" : templateExamples));
        EditorGUI.Separator();

        string blocked = EditorApplication.IsPlaying ? "Stop Play-In-Editor first: the running game may still hold these files."
            : templates.Length == 0 ? "The source template was not found. Write-back needs a checkout that contains OrbedenEditor/Templates."
            : !Directory.Exists(projectExamples) ? "This project has no Content/Examples directory." : string.Empty;
        EditorGUI.BeginDisabled(blocked.Length != 0);
        try
        {
            if (EditorGUI.Button("Write Back to Template (project -> template)"))
                report = EditorApplication.MirrorExamples(false);
            EditorGUI.SameLine();
            if (EditorGUI.Button("Reset from Template (template -> project)"))
            {
                report = EditorApplication.MirrorExamples(true);
                EditorAssetCatalog.Instance.Refresh();
            }
        }
        finally { EditorGUI.EndDisabled(); }
        if (blocked.Length != 0) EditorGUI.Label(blocked);
        EditorGUI.Separator();
        EditorGUI.Label(report.Length == 0 ? "No operation yet." : report);
    }
}
