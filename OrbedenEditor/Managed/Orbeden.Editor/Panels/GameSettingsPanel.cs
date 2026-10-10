using Orbeden;

namespace OrbedenEditor;

/// <summary>统一编辑项目级设置，当前提供层名称和物理碰撞矩阵。</summary>
internal sealed class ProjectSettingsPanel : EditorPanel
{
    private string root = "\0";
    private string[] names = [];
    private uint[] masks = [];
    private bool dirty;
    private string status = string.Empty;
    private int selectedLayer;

    public override EditorPanelInfo Info => new("project_settings", "Project Settings", false,
        new vector2(680, 650), PanelDockPlacement.Floating, 0.3f, 130);

    /// <summary>显式保存入口也参与关闭项目时的待保存检查。</summary>
    public override bool SavePendingChanges()
    {
        if (!dirty) return true;
        if (root != PathDefines.ContentRoot) return false;
        if (!EditorLayerSettings.Save(names, masks, out status)) return false;
        dirty = false;
        status = "Project settings saved.";
        return true;
    }

    /// <summary>绘制命名列表和对称矩阵行，修改保存在草稿中直到应用。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        if (string.IsNullOrWhiteSpace(PathDefines.ContentRoot)) { EditorGUI.Label("No project loaded."); return; }
        EditorLayerSettings.Refresh();
        if (root != PathDefines.ContentRoot)
        {
            root = PathDefines.ContentRoot;
            names = (string[])EditorLayerSettings.Names.Clone();
            masks = (uint[])EditorLayerSettings.Masks.Clone();
            dirty = false;
            status = string.Empty;
        }
        if (EditorLayerSettings.Error.Length != 0) EditorGUI.Label(EditorLayerSettings.Error);
        EditorGUI.Label(dirty ? "Project Settings (Unsaved)" : "Project Settings");
        EditorGUI.BeginDisabled(!EditorAssetsNative.CanModifyAssets());
        try
        {
            if (EditorGUI.Button("Apply")) SavePendingChanges();
            EditorGUI.SameLine();
            if (EditorGUI.Button("Revert"))
            {
                names = (string[])EditorLayerSettings.Names.Clone();
                masks = (uint[])EditorLayerSettings.Masks.Clone();
                dirty = false;
                status = string.Empty;
            }
            EditorGUI.SameLine();
            if (EditorGUI.Button("Restore Defaults"))
            {
                names = EditorLayerSettings.Defaults();
                masks = Enumerable.Repeat(uint.MaxValue, 32).ToArray();
                dirty = true;
            }
            if (status.Length != 0) EditorGUI.Label(status);
            int layers = NativeEditorGUI.TreeNode("Layers##settings_layers", false, false);
            if ((layers & 1) != 0)
            {
                try
                {
                    for (int index = 0; index < 32; ++index)
                        if (EditorGUI.InputText("Layer " + index, ref names[index])) dirty = true;
                }
                finally { NativeEditorGUI.TreePop(); }
            }
            int physics = NativeEditorGUI.TreeNode("Physics / Layer Collision Matrix##settings_physics", false, false);
            if ((physics & 1) != 0)
            {
                try
                {
                    EditorGUI.Label("Choose a matrix row. Every change also updates the matching column.");
                    EditorGUI.Label("Component collision masks further restrict these pairs. Triggers use this matrix too.");
                    if (EditorGUI.BeginCombo("Layer", $"{selectedLayer}: {names[selectedLayer]}"))
                    {
                        try
                        {
                            for (int index = 0; index < 32; ++index)
                                if (EditorGUI.Selectable($"{index}: {names[index]}", index == selectedLayer)) selectedLayer = index;
                        }
                        finally { EditorGUI.EndCombo(); }
                    }
                    if (EditorGUI.BeginTable("##collision_matrix", 2))
                    {
                        try
                        {
                            EditorGUI.TableSetupColumn("Collides With");
                            EditorGUI.TableSetupColumn("Enabled", 100, true);
                            EditorGUI.TableHeadersRow();
                            for (int index = 0; index < 32; ++index)
                            {
                                EditorGUI.TableNextRow();
                                EditorGUI.TableSetColumnIndex(0);
                                EditorGUI.Label($"{index}: {names[index]}");
                                EditorGUI.TableSetColumnIndex(1);
                                bool enabled = (masks[selectedLayer] & (1u << index)) != 0;
                                if (!EditorGUI.Checkbox("##pair_" + index, ref enabled)) continue;
                                masks[selectedLayer] = enabled ? masks[selectedLayer] | (1u << index) : masks[selectedLayer] & ~(1u << index);
                                masks[index] = enabled ? masks[index] | (1u << selectedLayer) : masks[index] & ~(1u << selectedLayer);
                                dirty = true;
                            }
                        }
                        finally { EditorGUI.EndTable(); }
                    }
                }
                finally { NativeEditorGUI.TreePop(); }
            }
        }
        finally { EditorGUI.EndDisabled(); }
    }
}
