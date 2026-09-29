using System;
using System.Numerics;
using System.Text;
using System.Runtime.InteropServices;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorApplicationNativeApi
{
    public IntPtr Context;
    public delegate* unmanaged[Cdecl]<IntPtr, void> RequestRepaint;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsPlaying;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, int> GetProjectText;
    public delegate* unmanaged[Cdecl]<IntPtr, int, void> RequestEditorAction;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetSelectedPlayerTarget;
    public delegate* unmanaged[Cdecl]<IntPtr, int, void> SetSelectedPlayerTarget;
    public delegate* unmanaged[Cdecl]<IntPtr, byte, byte, byte*, int, int> MirrorTemplate;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsWorldDirty;
    public delegate* unmanaged[Cdecl]<IntPtr, void> SetWorldDirty;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, void> RequestProjectAction;
    public delegate* unmanaged[Cdecl]<IntPtr, EditorWorldRenderSettingsAbi*, byte> GetWorldRenderSettings;
    public delegate* unmanaged[Cdecl]<IntPtr, EditorWorldRenderSettingsAbi*, void> SetWorldRenderSettings;
    public delegate* unmanaged[Cdecl]<IntPtr, int, uint, byte> ControlParticlePreview;
    public delegate* unmanaged[Cdecl]<IntPtr, int, ParticlePreviewInfoAbi*, byte> GetParticlePreviewInfo;
    public delegate* unmanaged[Cdecl]<IntPtr, byte, byte, byte, int, void> ScriptBuildCompleted;
}

/// <summary>编辑态粒子预览快照，布局与 ManagedEditorBridge.cpp 的 ParticlePreviewInfoAbi 一致。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct ParticlePreviewInfoAbi
{
    public uint State;
    public uint AliveCount;
    public uint TrailCount;
    public float Time;
    public ulong EmittedCount;
    public ulong RejectedCount;
}

#pragma warning restore CS0649

/// <summary>世界级渲染设置的读写载荷，布局与 ManagedEditorBridge.cpp 的 EditorWorldRenderSettingsAbi 一致。</summary>
[StructLayout(LayoutKind.Sequential)]
internal struct EditorWorldRenderSettingsAbi
{
    public IntPtr SkyboxKey;
    public int SkyboxKeyLength;
    public int SkyboxKeyReserved;
    public uint SkyboxEnabled;
    public uint Reserved;
    public float AmbientR;
    public float AmbientG;
    public float AmbientB;
    public float AmbientA;
    public float AmbientIntensity;
    public float ReflectionIntensity;
    public IntPtr ReflectionEnvironmentKey;
    public int ReflectionEnvironmentKeyLength;
    public int ReflectionEnvironmentKeyReserved;
    public uint SkyMode;
    public uint FogEnabled;
    public uint Quality;
    public uint AtmosphereReserved;
    public double PlanetCenterX;
    public double PlanetCenterY;
    public double PlanetCenterZ;
    public float MetersPerWorldUnit;
    public float AerosolDensity;
    public float SunRadianceScale;
    public float AtmosphereReservedTail;
    public uint DenseFogEnabled;
    public uint DenseFogReserved;
    public float FogVisibilityMeters;
    public float FogReferenceHeight;
    public float FogLayerHeight;
    public float FogFade;
    public float FogScatteringScale;
    public float FogSunScatteringScale;
    public float DenseFogReservedTail;
    public float FogTopFadeMeters;
    public uint FogDebugView;
    public float DenseFogReservedTail2;
}

/// <summary>世界级大气设置，字段与原生 RenderSettings::atmosphere 一一对应。</summary>
internal struct WorldAtmosphereSettings
{
    public uint SkyMode;
    public bool FogEnabled;
    public uint Quality;
    public double PlanetCenterX;
    public double PlanetCenterY;
    public double PlanetCenterZ;
    public float MetersPerWorldUnit;
    public float AerosolDensity;
    public float SunRadianceScale;
    public bool DenseFogEnabled;
    public float FogVisibilityMeters;
    public float FogReferenceHeight;
    public float FogLayerHeight;
    public float FogFade;
    public float FogScatteringScale;
    public float FogSunScatteringScale;
    public float FogTopFadeMeters;
    public uint FogDebugView;
}

internal enum EditorProjectField
{
    Name, Root, Content, World, Managed, Native, Repository, SourceTemplate, Status, PlayerTargets, ProjectFile
}

/// <summary>编辑器请求的种类：构建脚本/原生模块/Player，以及重导并重编的刷新。</summary>
internal enum EditorRequestKind { Scripts, Native, Player, Refresh }

/// <summary>模板内容目录位，与 ManagedEditorBridge.cpp 的 TemplateFolder* 常量对应。</summary>
internal enum EditorTemplateFolder { Examples = 1, Builtin = 2 }

/// <summary>Editor 应用级原生操作入口。</summary>
public static unsafe class EditorApplication
{
    /// <summary>请求打开项目文件或项目选择、新建对话框。</summary>
    internal static void RequestProjectAction(int action, string path = "")
    {
        byte[] bytes = Encoding.UTF8.GetBytes(path);
        fixed (byte* pointer = bytes) api.RequestProjectAction(api.Context, action, pointer, bytes.Length);
    }

    private static EditorApplicationNativeApi api;

    /// <summary>当前编辑 World 是否相对磁盘文件有未保存改动，真相源在原生 World。</summary>
    public static bool WorldDirty => api.IsWorldDirty != null && api.IsWorldDirty(api.Context) != 0;

    /// <summary>读取当前 World 的渲染设置。环境光是 sRGB 语义，与世界文件里存的一致。</summary>
    internal static bool TryGetWorldRenderSettings(out string skyboxKey, out bool skyboxEnabled, out Vector4 ambientColor,
        out float ambientIntensity, out string reflectionEnvironmentKey, out float reflectionIntensity,
        out WorldAtmosphereSettings atmosphere)
    {
        skyboxKey = string.Empty;
        skyboxEnabled = false;
        //读不到世界设置时的兜底，与 RenderSettings 的默认值保持一致
        ambientColor = new Vector4(0.34f, 0.37f, 0.42f, 1.0f);
        ambientIntensity = 1.0f;
        reflectionEnvironmentKey = string.Empty;
        reflectionIntensity = 1.0f;
        atmosphere = new WorldAtmosphereSettings
        {
            PlanetCenterY = -6371000.0,
            MetersPerWorldUnit = 1.0f,
            AerosolDensity = 1.0f,
            SunRadianceScale = 20.0f,
        };
        if (api.GetWorldRenderSettings == null) return false;

        EditorWorldRenderSettingsAbi settings;
        if (api.GetWorldRenderSettings(api.Context, &settings) == 0) return false;

        skyboxKey = settings.SkyboxKeyLength > 0 && settings.SkyboxKey != IntPtr.Zero
            ? Encoding.UTF8.GetString((byte*)settings.SkyboxKey, settings.SkyboxKeyLength)
            : string.Empty;
        skyboxEnabled = settings.SkyboxEnabled != 0;
        ambientColor = new Vector4(settings.AmbientR, settings.AmbientG, settings.AmbientB, settings.AmbientA);
        ambientIntensity = settings.AmbientIntensity;
        reflectionIntensity = settings.ReflectionIntensity;
        reflectionEnvironmentKey = settings.ReflectionEnvironmentKeyLength > 0 && settings.ReflectionEnvironmentKey != IntPtr.Zero
            ? Encoding.UTF8.GetString((byte*)settings.ReflectionEnvironmentKey, settings.ReflectionEnvironmentKeyLength)
            : string.Empty;
        atmosphere = new WorldAtmosphereSettings
        {
            SkyMode = settings.SkyMode,
            FogEnabled = settings.FogEnabled != 0,
            Quality = settings.Quality,
            PlanetCenterX = settings.PlanetCenterX,
            PlanetCenterY = settings.PlanetCenterY,
            PlanetCenterZ = settings.PlanetCenterZ,
            MetersPerWorldUnit = settings.MetersPerWorldUnit,
            AerosolDensity = settings.AerosolDensity,
            SunRadianceScale = settings.SunRadianceScale,
            DenseFogEnabled = settings.DenseFogEnabled != 0,
            FogVisibilityMeters = settings.FogVisibilityMeters,
            FogReferenceHeight = settings.FogReferenceHeight,
            FogLayerHeight = settings.FogLayerHeight,
            FogFade = settings.FogFade,
            FogScatteringScale = settings.FogScatteringScale,
            FogSunScatteringScale = settings.FogSunScatteringScale,
            FogTopFadeMeters = settings.FogTopFadeMeters,
            FogDebugView = settings.FogDebugView,
        };
        return true;
    }

    /// <summary>写入当前 World 的渲染设置。播放中由原生侧拒绝。</summary>
    internal static void SetWorldRenderSettings(string skyboxKey, bool skyboxEnabled, Vector4 ambientColor,
        float ambientIntensity, string reflectionEnvironmentKey, float reflectionIntensity, WorldAtmosphereSettings atmosphere)
    {
        if (api.SetWorldRenderSettings == null) return;

        byte[] bytes = Encoding.UTF8.GetBytes(skyboxKey ?? string.Empty);
        byte[] reflectionBytes = Encoding.UTF8.GetBytes(reflectionEnvironmentKey ?? string.Empty);
        fixed (byte* keyPointer = bytes)
        fixed (byte* reflectionPointer = reflectionBytes)
        {
            EditorWorldRenderSettingsAbi settings = new()
            {
                SkyboxKey = (IntPtr)keyPointer, SkyboxKeyLength = bytes.Length,
                SkyboxEnabled = skyboxEnabled ? 1u : 0u,
                AmbientR = ambientColor.X, AmbientG = ambientColor.Y, AmbientB = ambientColor.Z, AmbientA = ambientColor.W,
                AmbientIntensity = ambientIntensity, ReflectionIntensity = reflectionIntensity,
                ReflectionEnvironmentKey = (IntPtr)reflectionPointer, ReflectionEnvironmentKeyLength = reflectionBytes.Length,
                SkyMode = atmosphere.SkyMode,
                FogEnabled = atmosphere.FogEnabled ? 1u : 0u,
                Quality = atmosphere.Quality,
                PlanetCenterX = atmosphere.PlanetCenterX,
                PlanetCenterY = atmosphere.PlanetCenterY,
                PlanetCenterZ = atmosphere.PlanetCenterZ,
                MetersPerWorldUnit = atmosphere.MetersPerWorldUnit,
                AerosolDensity = atmosphere.AerosolDensity,
                SunRadianceScale = atmosphere.SunRadianceScale,
                DenseFogEnabled = atmosphere.DenseFogEnabled ? 1u : 0u,
                FogVisibilityMeters = atmosphere.FogVisibilityMeters,
                FogReferenceHeight = atmosphere.FogReferenceHeight,
                FogLayerHeight = atmosphere.FogLayerHeight,
                FogFade = atmosphere.FogFade,
                FogScatteringScale = atmosphere.FogScatteringScale,
                FogSunScatteringScale = atmosphere.FogSunScatteringScale,
                FogTopFadeMeters = atmosphere.FogTopFadeMeters,
                FogDebugView = atmosphere.FogDebugView,
            };
            api.SetWorldRenderSettings(api.Context, &settings);
        }
    }

    public static bool IsPlaying => api.IsPlaying != null && api.IsPlaying(api.Context) != 0;

    /// <summary>保存原生 Editor 应用 API，并校验环境设置载荷布局。</summary>
    internal static void Initialize(EditorApplicationNativeApi value)
    {
        api = value;
        CheckRenderSettingsLayout();
    }

    /// <summary>校验环境设置载荷的尺寸与关键偏移；托管与原生布局不一致时立刻中止。</summary>
    private static void CheckRenderSettingsLayout()
    {
        bool matches = Marshal.SizeOf<EditorWorldRenderSettingsAbi>() == 168
            && Marshal.OffsetOf<EditorWorldRenderSettingsAbi>(nameof(EditorWorldRenderSettingsAbi.SkyMode)).ToInt32() == 64
            && Marshal.OffsetOf<EditorWorldRenderSettingsAbi>(nameof(EditorWorldRenderSettingsAbi.PlanetCenterX)).ToInt32() == 80
            && Marshal.OffsetOf<EditorWorldRenderSettingsAbi>(nameof(EditorWorldRenderSettingsAbi.MetersPerWorldUnit)).ToInt32() == 104
            && Marshal.OffsetOf<EditorWorldRenderSettingsAbi>(nameof(EditorWorldRenderSettingsAbi.SunRadianceScale)).ToInt32() == 112
            && Marshal.OffsetOf<EditorWorldRenderSettingsAbi>(nameof(EditorWorldRenderSettingsAbi.DenseFogEnabled)).ToInt32() == 120
            && Marshal.OffsetOf<EditorWorldRenderSettingsAbi>(nameof(EditorWorldRenderSettingsAbi.FogVisibilityMeters)).ToInt32() == 128
            && Marshal.OffsetOf<EditorWorldRenderSettingsAbi>(nameof(EditorWorldRenderSettingsAbi.FogScatteringScale)).ToInt32() == 144
            && Marshal.OffsetOf<EditorWorldRenderSettingsAbi>(nameof(EditorWorldRenderSettingsAbi.FogSunScatteringScale)).ToInt32() == 148
            && Marshal.OffsetOf<EditorWorldRenderSettingsAbi>(nameof(EditorWorldRenderSettingsAbi.FogTopFadeMeters)).ToInt32() == 156
            && Marshal.OffsetOf<EditorWorldRenderSettingsAbi>(nameof(EditorWorldRenderSettingsAbi.FogDebugView)).ToInt32() == 160;
        if (!matches)
        {
            throw new InvalidOperationException("EditorWorldRenderSettingsAbi layout does not match the native bridge.");
        }
    }

    /// <summary>请求 Editor 在主线程重绘。</summary>
    public static void RequestRepaint()
    {
        if (api.RequestRepaint != null) api.RequestRepaint(api.Context);
    }

    //读取项目路径与构建状态
    internal static string GetProjectText(EditorProjectField field)
    {
        int length = api.GetProjectText(api.Context, (int)field, null, 0);
        byte[] bytes = new byte[length];
        fixed (byte* pointer = bytes) api.GetProjectText(api.Context, (int)field, pointer, length);
        return Encoding.UTF8.GetString(bytes);
    }

    //请求构建脚本、原生模块、Player，或重导并重编的刷新
    internal static void RequestEditorAction(EditorRequestKind kind) => api.RequestEditorAction(api.Context, (int)kind);

    /// <summary>向原生结算一次后台脚本构建：是否成功、是否取消、是否产出新程序集、重导的源文件数。</summary>
    internal static void CompleteScriptBuild(bool succeeded, bool cancelled, bool compiled, int reimportedCount)
    {
        if (api.ScriptBuildCompleted == null) return;
        api.ScriptBuildCompleted(api.Context, succeeded ? (byte)1 : (byte)0, cancelled ? (byte)1 : (byte)0,
            compiled ? (byte)1 : (byte)0, reimportedCount);
    }

    //读取当前构建目标
    internal static int GetSelectedPlayerTarget() => api.GetSelectedPlayerTarget(api.Context);

    //设置当前构建目标
    internal static void SetSelectedPlayerTarget(int index) => api.SetSelectedPlayerTarget(api.Context, index);

    //执行一次模板内容同步并读取结果，folders 选择参与迁移的目录
    internal static string MirrorTemplate(bool reset, EditorTemplateFolder folders)
    {
        byte[] bytes = new byte[16384];
        fixed (byte* pointer = bytes)
        {
            int length = api.MirrorTemplate(api.Context, reset ? (byte)1 : (byte)0, (byte)folders, pointer, bytes.Length);
            return Encoding.UTF8.GetString(bytes, 0, Math.Clamp(length, 0, bytes.Length));
        }
    }

    //标记 World 有改动。托管脚本字段的写入与撤销回放不经过原生钩子，只能由这里上报。
    internal static void MarkWorldDirty()
    {
        if (!IsPlaying && api.SetWorldDirty != null) api.SetWorldDirty(api.Context);
    }

    /// <summary>发起一次编辑态粒子预览命令；编辑态与 Play 态互斥。</summary>
    internal static bool ControlParticlePreview(int objectId, uint action)
    {
        if (api.Context == IntPtr.Zero || api.ControlParticlePreview == null || objectId <= 0) return false;
        return api.ControlParticlePreview(api.Context, objectId, action) != 0;
    }

    /// <summary>读取编辑态粒子预览快照，不启动模拟。</summary>
    internal static bool TryGetParticlePreviewInfo(int objectId, ref ParticlePreviewInfoAbi info)
    {
        info = default;
        if (api.Context == IntPtr.Zero || api.GetParticlePreviewInfo == null || objectId <= 0) return false;
        fixed (ParticlePreviewInfoAbi* pointer = &info) return api.GetParticlePreviewInfo(api.Context, objectId, pointer) != 0;
    }

}
