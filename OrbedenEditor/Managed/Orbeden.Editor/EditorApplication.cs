using System;
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
    public delegate* unmanaged[Cdecl]<IntPtr, byte, byte*, int, int> MirrorExamples;
}
#pragma warning restore CS0649

internal enum EditorProjectField
{
    Name, Root, Content, World, Managed, Native, Repository, SourceTemplate, Status, PlayerTargets
}

internal enum EditorBuildKind { Scripts, Native, Player }

/// <summary>Editor 应用级原生操作入口。</summary>
public static unsafe class EditorApplication
{
    private static EditorApplicationNativeApi api;
    public static bool WorldDirty { get; private set; }
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

    //执行一次示例同步并读取结果
    internal static string MirrorExamples(bool reset)
    {
        byte[] bytes = new byte[16384];
        fixed (byte* pointer = bytes)
        {
            int length = api.MirrorExamples(api.Context, reset ? (byte)1 : (byte)0, pointer, bytes.Length);
            return Encoding.UTF8.GetString(bytes, 0, Math.Clamp(length, 0, bytes.Length));
        }
    }

    internal static void MarkWorldDirty() { if (!IsPlaying) WorldDirty = true; }
    internal static void ClearWorldDirty() => WorldDirty = false;
    internal static void ClearDirty()
    {
        WorldDirty = false;
    }
}
