using System.Globalization;
using System.Numerics;
using Orbeden;

namespace OrbedenEditor;

/// <summary>背景天空的来源，取值与原生 SkyMode 一致。</summary>
internal enum SkyMode : uint { Cubemap = 0, Atmosphere = 1 }

/// <summary>大气积分质量档位，取值与原生 AtmosphereQuality 一致。</summary>
internal enum AtmosphereQuality : uint { Low = 0, Balanced = 1 }

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

    //大气草稿：地心三个分量用文本承载，失焦时才解析，输入中途的半截数字不会写进草稿
    private static SkyMode skyMode = SkyMode.Cubemap;
    private static bool atmosphereFogEnabled;
    private static AtmosphereQuality atmosphereQuality = AtmosphereQuality.Low;
    private static float aerosolDensity = 1.0f;
    private static float sunRadianceScale = 20.0f;
    private static float metersPerWorldUnit = 1.0f;
    private static double planetCenterX;
    private static double planetCenterY = -6371000.0;
    private static double planetCenterZ;
    private static string planetCenterXText = "0";
    private static string planetCenterYText = "-6371000";
    private static string planetCenterZText = "0";
    private static string fieldError = string.Empty;

    //浓雾草稿：能见度按 MOR 定义，单位米；雾层没有底面，雾顶以下都有雾
    private static bool denseFogEnabled;
    private static float fogVisibilityMeters = 200.0f;
    private static float fogTopHeightMeters = 100.0f;
    private static float fogScatteringScale = 1.0f;
    private static float fogSunScatteringScale = 1.0f;
    private static float fogTopFadeMeters = 20.0f;
    private static int fogDebugView;

    /// <summary>读出当前世界的设置，丢弃未提交的草稿。</summary>
    internal static void Reload()
    {
        loadedRoot = PathDefines.ContentRoot;
        dirty = false;
        status = string.Empty;
        fieldError = string.Empty;

        if (!EditorApplication.TryGetWorldRenderSettings(out string key, out bool enabled, out Vector4 ambient,
            out ambientIntensity, out string reflectionKey, out reflectionIntensity, out WorldAtmosphereSettings draft))
        {
            skybox = null;
            reflectionEnvironment = null;
            skyboxEnabled = false;
            //读不到世界设置时的兜底，与 RenderSettings 的默认值保持一致
            ambientColor = new color { r = 0.34f, g = 0.37f, b = 0.42f, a = 1.0f };
            skyMode = SkyMode.Cubemap;
            atmosphereFogEnabled = false;
            atmosphereQuality = AtmosphereQuality.Low;
            aerosolDensity = 1.0f;
            sunRadianceScale = 20.0f;
            metersPerWorldUnit = 1.0f;
            SetPlanetCenter(draft.PlanetCenterX, draft.PlanetCenterY, draft.PlanetCenterZ);
            denseFogEnabled = false;
            fogVisibilityMeters = 200.0f;
            fogTopHeightMeters = 100.0f;
            fogScatteringScale = 1.0f;
            fogSunScatteringScale = 1.0f;
            fogTopFadeMeters = 20.0f;
            fogDebugView = 0;
            status = "World render settings are unavailable.";
            return;
        }

        skybox = string.IsNullOrEmpty(key) ? null : EditorGUI.LoadObjectFieldAsset(typeof(Skybox), key) as Skybox;
        skyboxEnabled = enabled;
        reflectionEnvironment = string.IsNullOrEmpty(reflectionKey) ? null : EditorGUI.LoadObjectFieldAsset(typeof(Skybox), reflectionKey) as Skybox;
        ambientColor = new color { r = ambient.X, g = ambient.Y, b = ambient.Z, a = ambient.W };

        skyMode = (SkyMode)draft.SkyMode;
        atmosphereFogEnabled = draft.FogEnabled;
        atmosphereQuality = (AtmosphereQuality)draft.Quality;
        aerosolDensity = draft.AerosolDensity;
        sunRadianceScale = draft.SunRadianceScale;
        metersPerWorldUnit = draft.MetersPerWorldUnit;
        SetPlanetCenter(draft.PlanetCenterX, draft.PlanetCenterY, draft.PlanetCenterZ);

        denseFogEnabled = draft.DenseFogEnabled;
        fogVisibilityMeters = draft.FogVisibilityMeters;
        fogTopHeightMeters = draft.FogTopHeight;
        fogScatteringScale = draft.FogScatteringScale;
        fogSunScatteringScale = draft.FogSunScatteringScale;
        fogTopFadeMeters = draft.FogTopFadeMeters;
        fogDebugView = (int)draft.FogDebugView;
    }

    /// <summary>把地心同时写进数值草稿与文本草稿。</summary>
    private static void SetPlanetCenter(double x, double y, double z)
    {
        planetCenterX = x;
        planetCenterY = y;
        planetCenterZ = z;
        planetCenterXText = FromInvariant(x);
        planetCenterYText = FromInvariant(y);
        planetCenterZText = FromInvariant(z);
    }

    /// <summary>按 InvariantCulture 往返写出坐标文本。</summary>
    private static string FromInvariant(double value) => value.ToString("R", CultureInfo.InvariantCulture);

    /// <summary>解析一个地心分量；文本非法时保留旧值并记录字段错误，不打断正在输入的内容。</summary>
    private static void CommitPlanetCenter(string label, string text, ref double value)
    {
        if (double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double parsed) && double.IsFinite(parsed))
        {
            value = parsed;
            fieldError = string.Empty;
            return;
        }

        fieldError = label + " must be a finite number.";
    }

    /// <summary>绘制浓雾调试视图下拉框，返回是否发生改动。</summary>
    private static bool DrawFogDebugView(ref int value)
    {
        string[] labels =
        {
            "Off", "Hit / intervals", "Entry distance", "Fog path length",
            "Optical depth (0-5)", "Transmittance", "Fog scattering", "Front air",
        };

        bool changed = false;
        if (!EditorGUI.BeginCombo("Fog Debug View", labels[Math.Clamp(value, 0, labels.Length - 1)])) return false;
        try
        {
            for (int index = 0; index < labels.Length; ++index)
            {
                if (!EditorGUI.Selectable(labels[index], index == value)) continue;
                value = index;
                changed = true;
            }
        }
        finally { EditorGUI.EndCombo(); }
        return changed;
    }

    /// <summary>绘制枚举下拉框，返回是否发生改动。</summary>
    private static bool DrawEnum<T>(string label, ref T value) where T : struct, Enum
    {
        bool changed = false;
        if (!EditorGUI.BeginCombo(label, value.ToString())) return false;
        try
        {
            foreach (T option in Enum.GetValues<T>())
            {
                if (!EditorGUI.Selectable(option.ToString(), option.Equals(value))) continue;
                value = option;
                changed = true;
            }
        }
        finally { EditorGUI.EndCombo(); }
        return changed;
    }

    /// <summary>收集当前草稿并检查合法性；非法配置不允许提交。</summary>
    private static bool TryBuildAtmosphere(out WorldAtmosphereSettings atmosphere)
    {
        atmosphere = new WorldAtmosphereSettings
        {
            SkyMode = (uint)skyMode,
            FogEnabled = atmosphereFogEnabled,
            Quality = (uint)atmosphereQuality,
            PlanetCenterX = planetCenterX,
            PlanetCenterY = planetCenterY,
            PlanetCenterZ = planetCenterZ,
            MetersPerWorldUnit = metersPerWorldUnit,
            AerosolDensity = aerosolDensity,
            SunRadianceScale = sunRadianceScale,
            DenseFogEnabled = denseFogEnabled,
            FogVisibilityMeters = fogVisibilityMeters,
            FogTopHeight = fogTopHeightMeters,
            FogScatteringScale = fogScatteringScale,
            FogSunScatteringScale = fogSunScatteringScale,
            FogTopFadeMeters = fogTopFadeMeters,
            FogDebugView = (uint)fogDebugView,
        };

        if (!double.IsFinite(planetCenterX) || !double.IsFinite(planetCenterY) || !double.IsFinite(planetCenterZ))
        {
            fieldError = "Planet center must be a finite number.";
            return false;
        }
        if (!float.IsFinite(metersPerWorldUnit) || metersPerWorldUnit <= 0.0f)
        {
            fieldError = "Meters Per World Unit must be greater than zero.";
            return false;
        }
        if (!float.IsFinite(aerosolDensity) || !float.IsFinite(sunRadianceScale))
        {
            fieldError = "Atmosphere densities must be finite numbers.";
            return false;
        }
        if (!float.IsFinite(fogVisibilityMeters) || fogVisibilityMeters <= 0.0f)
        {
            fieldError = "Dense Fog Visibility must be greater than zero.";
            return false;
        }
        if (!float.IsFinite(fogTopHeightMeters) || !float.IsFinite(fogScatteringScale)
            || !float.IsFinite(fogSunScatteringScale)
            || !float.IsFinite(fogTopFadeMeters) || fogTopFadeMeters < 0.0f
            || fogDebugView < 0 || fogDebugView > 7)
        {
            fieldError = "Dense fog heights must be finite numbers.";
            return false;
        }
        return true;
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
        if (!TryBuildAtmosphere(out WorldAtmosphereSettings atmosphere))
        {
            status = fieldError;
            return false;
        }

        EditorApplication.SetWorldRenderSettings(
            skybox?.GetInstanceId() ?? string.Empty,
            skyboxEnabled,
            new Vector4(ambientColor.r, ambientColor.g, ambientColor.b, ambientColor.a), ambientIntensity,
            reflectionEnvironment?.GetInstanceId() ?? string.Empty, reflectionIntensity, atmosphere);
        dirty = false;
        status = string.Empty;
        fieldError = string.Empty;
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
                if (EditorGUI.Checkbox("Skybox Enabled", ref skyboxEnabled)) dirty = true;
                SkyMode selectedMode = skyMode;
                if (DrawEnum("Sky Mode", ref selectedMode)) { skyMode = selectedMode; dirty = true; }
                if (EditorGUI.ObjectField<Skybox>("Skybox", ref selected)) { skybox = selected; dirty = true; }
                //与 Inspector 的颜色字段共用同一个原语：RGB 与 Alpha 在同一行、带棋盘预览
                if (GUI.ColorField("Ambient Color", ref ambientColor)) dirty = true;
                if (EditorGUI.InputFloat("Ambient Intensity", ref ambientIntensity))
                {
                    ambientIntensity = Math.Max(0.0f, ambientIntensity);
                    dirty = true;
                }
                EditorGUI.Label("Color sets the tint; intensity scales brightness linearly (0 = off, 1 = baseline).");

                EditorGUI.Separator();
                if (EditorGUI.Checkbox("Atmosphere Fog", ref atmosphereFogEnabled)) dirty = true;

                //六面天空盒不参与大气散射，加雾的表面与天空在地平线处接不上
                if (atmosphereFogEnabled && skyMode == SkyMode.Cubemap)
                {
                    EditorGUI.TextColored("Cubemap sky ignores the atmosphere; use Sky Mode Atmosphere.",
                        EditorTheme.Current.LogError);
                }

                //雾关闭时这组参数整块收起，勾上后始终展开，不提供手动收放
                if (atmosphereFogEnabled)
                {
                    AtmosphereQuality selectedQuality = atmosphereQuality;
                    if (DrawEnum("Quality", ref selectedQuality)) { atmosphereQuality = selectedQuality; dirty = true; }
                    if (EditorGUI.InputFloat("Aerosol Density", ref aerosolDensity))
                    {
                        aerosolDensity = Math.Clamp(aerosolDensity, 0.0f, 8.0f);
                        dirty = true;
                    }
                    if (EditorGUI.InputFloat("Sun Radiance Scale", ref sunRadianceScale))
                    {
                        sunRadianceScale = Math.Clamp(sunRadianceScale, 0.0f, 100.0f);
                        dirty = true;
                    }
                    if (EditorGUI.InputText("Planet Center X", ref planetCenterXText)) { CommitPlanetCenter("Planet Center X", planetCenterXText, ref planetCenterX); dirty = true; }
                    if (EditorGUI.InputText("Planet Center Y", ref planetCenterYText)) { CommitPlanetCenter("Planet Center Y", planetCenterYText, ref planetCenterY); dirty = true; }
                    if (EditorGUI.InputText("Planet Center Z", ref planetCenterZText)) { CommitPlanetCenter("Planet Center Z", planetCenterZText, ref planetCenterZ); dirty = true; }
                    if (EditorGUI.InputFloat("Meters Per World Unit", ref metersPerWorldUnit))
                    {
                        metersPerWorldUnit = Math.Clamp(metersPerWorldUnit, 0.0001f, 10000.0f);
                        dirty = true;
                    }
                    EditorGUI.Label("Aerial perspective is thin haze over kilometres. Shared with the Atmosphere sky mode.");
                }

                EditorGUI.Separator();
                if (EditorGUI.Checkbox("Dense Fog", ref denseFogEnabled)) dirty = true;

                //总开关关闭时浓雾整体不生效，参数同样整块收起
                if (atmosphereFogEnabled && denseFogEnabled)
                {
                    if (EditorGUI.InputFloat("Visibility (m)", ref fogVisibilityMeters))
                    {
                        fogVisibilityMeters = Math.Clamp(fogVisibilityMeters, 10.0f, 100000.0f);
                        dirty = true;
                    }
                    if (EditorGUI.InputFloat("Fog Top Height (m)", ref fogTopHeightMeters))
                    {
                        fogTopHeightMeters = Math.Clamp(fogTopHeightMeters, -1000.0f, 20000.0f);
                        dirty = true;
                    }
                    EditorGUI.Label("Fog fills every height below the top; the value is relative to the reference sphere.");
                    if (EditorGUI.InputFloat("Fog Top Fade (m)", ref fogTopFadeMeters))
                    {
                        fogTopFadeMeters = Math.Clamp(fogTopFadeMeters, 0.0f, 5000.0f);
                        dirty = true;
                    }
                    if (EditorGUI.InputFloat("Fog Ambient Scattering", ref fogScatteringScale))
                    {
                        fogScatteringScale = Math.Clamp(fogScatteringScale, 0.0f, 8.0f);
                        dirty = true;
                    }
                    if (EditorGUI.InputFloat("Fog Sun Scattering", ref fogSunScatteringScale))
                    {
                        fogSunScatteringScale = Math.Clamp(fogSunScatteringScale, 0.0f, 8.0f);
                        dirty = true;
                    }
                    //提示雾顶衰减尺度的雾顶限制
                    if (fogTopFadeMeters > fogTopHeightMeters)
                    {
                        EditorGUI.TextColored("Top Fade exceeds the fog top; it is clamped to the fog top.",
                            EditorTheme.Current.LogWarning);
                    }
                    EditorGUI.Label("Top Fade keeps about 5% density at the nominal top and adds a thin tail above it. Zero gives a hard top.");
                    if (DrawFogDebugView(ref fogDebugView)) dirty = true;
                    EditorGUI.Label("Visibility is MOR (5% transmission) and covers the fog's own extinction, not the background atmosphere. "
                        + "Scattered light is the world ambient plus the main light scaled by the two values above; both fade out at night.");
                }

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
                if (fieldError.Length != 0) EditorGUI.Label(fieldError);
                if (status.Length != 0) EditorGUI.Label(status);
            }
            finally { EditorGUI.EndDisabled(); }
        }
        finally { EditorGUI.PopId(); }
    }
}
