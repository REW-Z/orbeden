using Orbeden;

namespace OrbedenEditor;

/// <summary>显示项目构建信息并调用原生构建服务。</summary>
internal sealed class BuildGamePanel : EditorPanel
{
    public override EditorPanelInfo Info => new("build_game", "Build Game", true,
        new vector2(360, 180), PanelDockPlacement.Floating, 0.25f, 110);

    /// <summary>绘制项目路径、构建目标和构建状态。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        string root = EditorApplication.GetProjectText(EditorProjectField.Root);
        if (root.Length == 0) EditorGUI.Label("No project loaded.");
        else
        {
            foreach (EditorProjectField field in new[] { EditorProjectField.Name, EditorProjectField.Root,
                EditorProjectField.Content, EditorProjectField.World, EditorProjectField.Managed, EditorProjectField.Native })
                EditorGUI.Label(field + ": " + EditorApplication.GetProjectText(field));
        }
        EditorGUI.Separator();
        EditorGUI.BeginDisabled(root.Length == 0);
        try
        {
            if (EditorGUI.Button("Build Game C#")) EditorApplication.RequestBuild(EditorBuildKind.Scripts);
            EditorGUI.SameLine();
            if (EditorGUI.Button("Build Game C++")) EditorApplication.RequestBuild(EditorBuildKind.Native);

            //显示平台可用性并保存选择
            string[] targets = EditorApplication.GetProjectText(EditorProjectField.PlayerTargets).Split('\0');
            int selected = EditorApplication.GetSelectedPlayerTarget();
            string preview = selected >= 0 && selected * 2 < targets.Length ? targets[selected * 2] : string.Empty;
            if (EditorGUI.BeginCombo("Target Platform", preview))
            {
                try
                {
                    for (int index = 0; index * 2 + 1 < targets.Length; ++index)
                    {
                        EditorGUI.BeginDisabled(targets[index * 2 + 1] != "1");
                        try
                        {
                            if (EditorGUI.Selectable(targets[index * 2], index == selected))
                                EditorApplication.SetSelectedPlayerTarget(index);
                        }
                        finally { EditorGUI.EndDisabled(); }
                    }
                }
                finally { EditorGUI.EndCombo(); }
            }
            EditorGUI.SameLine();
            if (EditorGUI.Button("Build Player")) EditorApplication.RequestBuild(EditorBuildKind.Player);
        }
        finally { EditorGUI.EndDisabled(); }
        string status = EditorApplication.GetProjectText(EditorProjectField.Status);
        if (status.Length == 0) return;
        EditorGUI.Separator();
        EditorGUI.Label(status);
    }
}
