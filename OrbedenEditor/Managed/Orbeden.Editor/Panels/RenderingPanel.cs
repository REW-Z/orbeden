using System.Numerics;
using Orbeden;

namespace OrbedenEditor;

/// <summary>统一编辑渲染与显示参数：世界级的环境光与天空盒，项目级的曝光。
/// 曝光描述的是观察方式，与场景里的灯光强度互不影响，所以它存在项目设置里而不是世界文件里。</summary>
internal sealed class RenderingPanel : EditorPanel
{
    private string root = "\0";
    private bool dirty;
    private string status = string.Empty;

    //草稿值。改动累积到 Apply 才落盘，避免拖动数值时反复写文件。
    private Skybox? skybox;
    private bool skyboxEnabled;
    private vector3 ambientRgb;
    private float ambientAlpha = 1.0f;
    private float exposure = EditorDisplaySettings.DefaultExposure;

    public override EditorPanelInfo Info => new("rendering", "Rendering", false,
        new vector2(520, 420), PanelDockPlacement.Floating, 0.35f, 140);

    /// <summary>关闭项目时把未应用的改动落盘。</summary>
    public override bool SavePendingChanges()
    {
        if (!dirty) return true;
        if (root != PathDefines.ContentRoot) return false;
        if (!Apply()) return false;
        status = "Rendering settings saved.";
        return true;
    }

    /// <summary>从世界与项目设置重新读取草稿值。</summary>
    private void ReloadFromSources()
    {
        root = PathDefines.ContentRoot;
        EditorDisplaySettings.Refresh();

        string key;
        bool enabled;
        Vector4 ambient;
        if (!EditorApplication.TryGetWorldRenderSettings(out key, out enabled, out ambient))
        {
            status = "World render settings are unavailable.";
            return;
        }

        skybox = string.IsNullOrEmpty(key) ? null : EditorGUI.LoadObjectFieldAsset(typeof(Skybox), key) as Skybox;
        skyboxEnabled = enabled;
        ambientRgb = new vector3(ambient.X, ambient.Y, ambient.Z);
        ambientAlpha = ambient.W;
        exposure = EditorDisplaySettings.Exposure;
        dirty = false;
        status = string.Empty;
    }

    /// <summary>把草稿写回世界与项目设置。</summary>
    private bool Apply()
    {
        if (!EditorAssetsNative.CanModifyAssets()) { status = "Rendering settings cannot be changed while playing."; return false; }

        EditorApplication.SetWorldRenderSettings(
            skybox?.GetInstanceId() ?? string.Empty,
            skyboxEnabled,
            new Vector4(ambientRgb.x, ambientRgb.y, ambientRgb.z, ambientAlpha));
        if (!EditorDisplaySettings.Save(exposure, out string error)) { status = error; return false; }

        dirty = false;
        return true;
    }

    /// <summary>绘制环境与显示两组参数，改动保存在草稿中直到应用。</summary>
    protected override void DrawContent(EditorPanelContext context)
    {
        if (string.IsNullOrWhiteSpace(PathDefines.ContentRoot)) { EditorGUI.Label("No project loaded."); return; }
        if (root != PathDefines.ContentRoot) ReloadFromSources();

        if (EditorDisplaySettings.Error.Length != 0) EditorGUI.Label(EditorDisplaySettings.Error);
        EditorGUI.Label(dirty ? "Rendering (Unsaved)" : "Rendering");
        EditorGUI.BeginDisabled(!EditorAssetsNative.CanModifyAssets());
        try
        {
            if (EditorGUI.Button("Apply")) Apply();
            EditorGUI.SameLine();
            if (EditorGUI.Button("Revert")) ReloadFromSources();
            if (status.Length != 0) EditorGUI.Label(status);

            int environment = NativeEditorGUI.TreeNode("Environment##rendering_environment", false, false);
            if ((environment & 1) != 0)
            {
                try
                {
                    EditorGUI.Label("World-level settings, stored in the current world file.");
                    Skybox? selected = skybox;
                    if (EditorGUI.ObjectField<Skybox>("Skybox", ref selected)) { skybox = selected; dirty = true; }
                    if (EditorGUI.Checkbox("Skybox Enabled", ref skyboxEnabled)) dirty = true;
                    if (EditorGUI.InputVector3("Ambient Color RGB", ref ambientRgb)) dirty = true;
                    if (EditorGUI.InputFloat("Ambient Color Alpha", ref ambientAlpha)) dirty = true;
                    EditorGUI.Label("Colors are sRGB here; the pipeline converts them to linear for lighting.");
                }
                finally { NativeEditorGUI.TreePop(); }
            }

            int display = NativeEditorGUI.TreeNode("Display##rendering_display", false, false);
            if ((display & 1) != 0)
            {
                try
                {
                    EditorGUI.Label("Project-level settings, stored in ProjectSettings.display.");
                    if (EditorGUI.InputFloat("Exposure", ref exposure)) dirty = true;
                    EditorGUI.Label("Exposure is how the scene is observed, not how bright its lights are.");
                    EditorGUI.Label("A Camera component can override it per camera.");
                }
                finally { NativeEditorGUI.TreePop(); }
            }
        }
        finally { EditorGUI.EndDisabled(); }
    }
}
