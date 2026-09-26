using System.Numerics;
using Orbeden;

namespace OrbedenEditor;

/// <summary>
/// 世界级渲染设置（天空盒、环境光）的编辑控件。
///
/// 这组字段不在任何 Ens 上，EnsView 够不着，因此 RenderingPanel 与资源 Inspector 都要提供入口。
/// 两处共用这一份实现——同一组字段出现两套绘制迟早会漂移。
///
/// 草稿是静态的：两个宿主同时可见时看到的是同一份未提交值，不会互相覆盖。
/// 控件 ID 由 idScope 隔离，避免同一帧内两处绘制撞上 ImGui 的 ID。
/// </summary>
internal static class EditorEnvironmentSettings
{
    private static string loadedRoot = "\0";
    private static bool dirty;
    private static string status = string.Empty;
    private static Skybox? skybox;
    private static bool skyboxEnabled;
    private static vector3 ambientRgb;
    private static float ambientAlpha = 1.0f;

    /// <summary>读出当前世界的设置，丢弃未提交的草稿。</summary>
    internal static void Reload()
    {
        loadedRoot = PathDefines.ContentRoot;
        dirty = false;
        status = string.Empty;

        if (!EditorApplication.TryGetWorldRenderSettings(out string key, out bool enabled, out Vector4 ambient))
        {
            skybox = null;
            skyboxEnabled = false;
            ambientRgb = new vector3(0.08f, 0.09f, 0.1f);
            ambientAlpha = 1.0f;
            status = "World render settings are unavailable.";
            return;
        }

        skybox = string.IsNullOrEmpty(key) ? null : EditorGUI.LoadObjectFieldAsset(typeof(Skybox), key) as Skybox;
        skyboxEnabled = enabled;
        ambientRgb = new vector3(ambient.X, ambient.Y, ambient.Z);
        ambientAlpha = ambient.W;
    }

    /// <summary>切项目后丢弃草稿，下次绘制重新读取。</summary>
    internal static void Invalidate() => loadedRoot = "\0";

    private static void EnsureLoaded()
    {
        if (loadedRoot != PathDefines.ContentRoot) Reload();
    }

    /// <summary>是否有未提交的改动，供宿主面板的关项目检查使用。</summary>
    internal static bool HasPendingChanges => dirty;

    /// <summary>提交草稿。宿主面板关闭项目前也会调用。</summary>
    internal static bool Apply()
    {
        if (!dirty) return true;
        if (!EditorAssetsNative.CanModifyAssets())
        {
            status = "World render settings cannot be changed while playing.";
            return false;
        }

        EditorApplication.SetWorldRenderSettings(
            skybox?.GetInstanceId() ?? string.Empty,
            skyboxEnabled,
            new Vector4(ambientRgb.x, ambientRgb.y, ambientRgb.z, ambientAlpha));
        dirty = false;
        status = string.Empty;
        return true;
    }

    /// <summary>绘制设置字段与 Apply/Revert。idScope 隔离同帧多个宿主的控件 ID。</summary>
    internal static void Draw(string idScope)
    {
        EnsureLoaded();
        EditorGUI.PushId(idScope);
        try
        {
            EditorGUI.BeginDisabled(!EditorAssetsNative.CanModifyAssets());
            try
            {
                Skybox? selected = skybox;
                if (EditorGUI.ObjectField<Skybox>("Skybox", ref selected)) { skybox = selected; dirty = true; }
                if (EditorGUI.Checkbox("Skybox Enabled", ref skyboxEnabled)) dirty = true;
                if (EditorGUI.InputVector3("Ambient Color RGB", ref ambientRgb)) dirty = true;
                if (EditorGUI.InputFloat("Ambient Color Alpha", ref ambientAlpha)) dirty = true;
                EditorGUI.Label("Colors are sRGB here; the pipeline converts them to linear for lighting.");

                EditorGUI.Separator();
                if (EditorGUI.Button(dirty ? "Apply *" : "Apply")) Apply();
                EditorGUI.SameLine();
                if (EditorGUI.Button("Revert")) Reload();
                if (status.Length != 0) EditorGUI.Label(status);
            }
            finally { EditorGUI.EndDisabled(); }
        }
        finally { EditorGUI.PopId(); }
    }
}
