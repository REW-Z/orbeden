using Orbeden;

namespace OrbedenEditor;

/// <summary>管理项目示例与引擎模板之间的同步。</summary>
internal sealed class DevPanel : EditorPanel
{
    private string report = string.Empty;

    public override EditorPanelInfo Info => new("dev", "Dev", false,
        new vector2(560, 340), PanelDockPlacement.Floating, 0.25f, 120);

    /// <summary>绘制示例与默认资源目录的同步操作。</summary>
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
        string projectBuiltin = Path.Combine(content, "Builtin");
        string templateExamples = templates.Length == 0 ? string.Empty : Path.Combine(templates, "Examples");
        string templateBuiltin = templates.Length == 0 ? string.Empty : Path.Combine(templates, "Builtin");
        EditorGUI.Label("Project examples: " + projectExamples);
        EditorGUI.Label("Project builtin: " + projectBuiltin);
        EditorGUI.Label("Repository root: " + EditorApplication.GetProjectText(EditorProjectField.Repository));
        EditorGUI.Label("Template examples: " + (templateExamples.Length == 0 ? "(not found)" : templateExamples));
        EditorGUI.Label("Template builtin: " + (templateBuiltin.Length == 0 ? "(not found)" : templateBuiltin));
        EditorGUI.Separator();

        //三条路径各自判定：写回要项目侧有示例，重置只看模板侧对应目录在不在。
        string commonBlocked = EditorApplication.IsPlaying ? "Stop Play-In-Editor first: the running game may still hold these files."
            : templates.Length == 0 ? "The source template was not found. These operations need a checkout that contains OrbedenEditor/Templates."
            : string.Empty;
        string writeBackBlocked = commonBlocked.Length != 0 ? commonBlocked
            : !Directory.Exists(projectExamples) ? "This project has no Content/Examples directory." : string.Empty;
        string resetExamplesBlocked = commonBlocked.Length != 0 ? commonBlocked
            : !Directory.Exists(templateExamples) ? "The template has no Examples directory." : string.Empty;
        string resetBuiltinBlocked = commonBlocked.Length != 0 ? commonBlocked
            : !Directory.Exists(templateBuiltin) ? "The template has no Builtin directory." : string.Empty;

        //写回是成对操作：示例引用 Builtin，只写回一半会让项目里的材质指向不存在的默认资源。
        EditorGUI.BeginDisabled(writeBackBlocked.Length != 0);
        try
        {
            if (EditorGUI.Button("Write Back to Template (project -> template)"))
                report = EditorApplication.MirrorTemplate(false, EditorTemplateFolder.Examples | EditorTemplateFolder.Builtin);
        }
        finally { EditorGUI.EndDisabled(); }
        if (writeBackBlocked.Length != 0) EditorGUI.Label(writeBackBlocked);
        EditorGUI.Separator();

        //重置拆成两条独立路径，改过哪一侧就只回退哪一侧。
        EditorGUI.BeginDisabled(resetExamplesBlocked.Length != 0);
        try
        {
            if (EditorGUI.Button("Reset Examples (template -> project)"))
                ResetFromTemplate(EditorTemplateFolder.Examples);
        }
        finally { EditorGUI.EndDisabled(); }
        EditorGUI.SameLine();
        EditorGUI.BeginDisabled(resetBuiltinBlocked.Length != 0);
        try
        {
            if (EditorGUI.Button("Reset Builtin (template -> project)"))
                ResetFromTemplate(EditorTemplateFolder.Builtin);
        }
        finally { EditorGUI.EndDisabled(); }
        if (resetExamplesBlocked.Length != 0) EditorGUI.Label(resetExamplesBlocked);
        if (resetBuiltinBlocked.Length != 0) EditorGUI.Label(resetBuiltinBlocked);
        EditorGUI.Separator();
        EditorGUI.Label(report.Length == 0 ? "No operation yet." : report);
    }

    /// <summary>把选中的模板目录重置到项目，并刷新资产目录。</summary>
    private void ResetFromTemplate(EditorTemplateFolder folders)
    {
        report = EditorApplication.MirrorTemplate(true, folders);
        EditorAssetCatalog.Instance.Refresh();
    }
}
