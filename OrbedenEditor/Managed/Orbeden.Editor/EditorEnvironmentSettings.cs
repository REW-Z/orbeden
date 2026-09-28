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
    private static color ambientColor = new() { r = 0.34f, g = 0.37f, b = 0.42f, a = 1.0f };
    private static float ambientIntensity = 1.0f;
    private static Skybox? reflectionEnvironment;
    private static float reflectionIntensity = 1.0f;

    /// <summary>读出当前世界的设置，丢弃未提交的草稿。</summary>
    internal static void Reload()
    {
        loadedRoot = PathDefines.ContentRoot;
        dirty = false;
        status = string.Empty;

        if (!EditorApplication.TryGetWorldRenderSettings(out string key, out bool enabled, out Vector4 ambient,
            out ambientIntensity, out string reflectionKey, out reflectionIntensity))
        {
            skybox = null;
            reflectionEnvironment = null;
            skyboxEnabled = false;
            //读不到世界设置时的兜底，与 RenderSettings 的默认值保持一致
            ambientColor = new color { r = 0.34f, g = 0.37f, b = 0.42f, a = 1.0f };
            status = "World render settings are unavailable.";
            return;
        }

        skybox = string.IsNullOrEmpty(key) ? null : EditorGUI.LoadObjectFieldAsset(typeof(Skybox), key) as Skybox;
        skyboxEnabled = enabled;
        reflectionEnvironment = string.IsNullOrEmpty(reflectionKey) ? null : EditorGUI.LoadObjectFieldAsset(typeof(Skybox), reflectionKey) as Skybox;
        ambientColor = new color { r = ambient.X, g = ambient.Y, b = ambient.Z, a = ambient.W };
    }

    /// <summary>切项目后丢弃草稿，下次绘制重新读取。
    /// 必须连未提交改动一起丢掉：草稿属于上一个项目的世界，没有地方可以落盘，
    /// 留着只会让宿主的保存检查一直以为有待写数据。</summary>
    internal static void Invalidate()
    {
        loadedRoot = "\0";
        dirty = false;
        status = string.Empty;
    }

    private static void EnsureLoaded()
    {
        if (loadedRoot != PathDefines.ContentRoot) Reload();
    }

    /// <summary>是否有属于**当前项目**的未提交改动。
    /// 草稿属于别的项目时不算数：它没有地方可以落盘，也不该挡住别人的保存流程。</summary>
    internal static bool HasPendingChanges => dirty && loadedRoot == PathDefines.ContentRoot;

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
            new Vector4(ambientColor.r, ambientColor.g, ambientColor.b, ambientColor.a), ambientIntensity,
            reflectionEnvironment?.GetInstanceId() ?? string.Empty, reflectionIntensity);
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
                //与 Inspector 的颜色字段共用同一个原语：RGB 与 Alpha 在同一行、带棋盘预览
                if (GUI.ColorField("Ambient Color", ref ambientColor)) dirty = true;
                if (EditorGUI.InputFloat("Ambient Intensity", ref ambientIntensity))
                {
                    ambientIntensity = Math.Max(0.0f, ambientIntensity);
                    dirty = true;
                }
                EditorGUI.Label("Color sets the tint; intensity scales brightness linearly (0 = off, 1 = baseline).");

                EditorGUI.Separator();
                Skybox? selectedReflection = reflectionEnvironment;
                if (EditorGUI.ObjectField<Skybox>("Reflection Environment", ref selectedReflection))
                {
                    reflectionEnvironment = selectedReflection;
                    dirty = true;
                }
                if (EditorGUI.InputFloat("Reflection Intensity", ref reflectionIntensity))
                {
                    reflectionIntensity = Math.Max(0.0f, reflectionIntensity);
                    dirty = true;
                }
                EditorGUI.Label("Empty uses the skybox, even when its background is hidden. Intensity 0 disables reflections.");

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
