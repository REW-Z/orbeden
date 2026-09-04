using System;
using System.Runtime.InteropServices;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct EditorApplicationNativeApi
{
    public IntPtr Context;
    public delegate* unmanaged[Cdecl]<IntPtr, void> RequestRepaint;
    public delegate* unmanaged[Cdecl]<IntPtr, byte> IsPlaying;
}
#pragma warning restore CS0649

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

    internal static void MarkWorldDirty() { if (!IsPlaying) WorldDirty = true; }
    internal static void ClearWorldDirty() => WorldDirty = false;
    internal static void ClearDirty()
    {
        WorldDirty = false;
    }
}
