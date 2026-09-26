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
    public delegate* unmanaged[Cdecl]<IntPtr, int, void> RequestBuild;
    public delegate* unmanaged[Cdecl]<IntPtr, int> GetSelectedPlayerTarget;
    public delegate* unmanaged[Cdecl]<IntPtr, int, void> SetSelectedPlayerTarget;
    public delegate* unmanaged[Cdecl]<IntPtr, byte, byte, byte*, int, int> MirrorTemplate;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsWorldDirty;
    public delegate* unmanaged[Cdecl]<IntPtr, void> SetWorldDirty;
    public delegate* unmanaged[Cdecl]<IntPtr, int, byte*, int, void> RequestProjectAction;
    public delegate* unmanaged[Cdecl]<IntPtr, EditorWorldRenderSettingsAbi*, byte> GetWorldRenderSettings;
    public delegate* unmanaged[Cdecl]<IntPtr, byte*, int, byte, float*, void> SetWorldRenderSettings;
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
}

internal enum EditorProjectField
{
    Name, Root, Content, World, Managed, Native, Repository, SourceTemplate, Status, PlayerTargets, ProjectFile
}

internal enum EditorBuildKind { Scripts, Native, Player }

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
    internal static bool TryGetWorldRenderSettings(out string skyboxKey, out bool skyboxEnabled, out Vector4 ambientColor)
    {
        skyboxKey = string.Empty;
        skyboxEnabled = false;
        ambientColor = new Vector4(0.08f, 0.09f, 0.1f, 1.0f);
        if (api.GetWorldRenderSettings == null) return false;

        EditorWorldRenderSettingsAbi settings;
        if (api.GetWorldRenderSettings(api.Context, &settings) == 0) return false;

        skyboxKey = settings.SkyboxKeyLength > 0 && settings.SkyboxKey != IntPtr.Zero
            ? Encoding.UTF8.GetString((byte*)settings.SkyboxKey, settings.SkyboxKeyLength)
            : string.Empty;
        skyboxEnabled = settings.SkyboxEnabled != 0;
        ambientColor = new Vector4(settings.AmbientR, settings.AmbientG, settings.AmbientB, settings.AmbientA);
        return true;
    }

    /// <summary>写入当前 World 的渲染设置。播放中由原生侧拒绝。</summary>
    internal static void SetWorldRenderSettings(string skyboxKey, bool skyboxEnabled, Vector4 ambientColor)
    {
        if (api.SetWorldRenderSettings == null) return;

        byte[] bytes = Encoding.UTF8.GetBytes(skyboxKey ?? string.Empty);
        fixed (byte* keyPointer = bytes)
        {
            float* ambient = stackalloc float[4] { ambientColor.X, ambientColor.Y, ambientColor.Z, ambientColor.W };
            api.SetWorldRenderSettings(api.Context, keyPointer, bytes.Length, (byte)(skyboxEnabled ? 1 : 0), ambient);
        }
    }

    public static bool IsPlaying => api.IsPlaying != null && api.IsPlaying(api.Context) != 0;

    /// <summary>保存原生 Editor 应用 API。</summary>
    internal static void Initialize(EditorApplicationNativeApi value)
    {
        api = value;
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

    //请求构建脚本、原生模块或 Player
    internal static void RequestBuild(EditorBuildKind kind) => api.RequestBuild(api.Context, (int)kind);

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
}
