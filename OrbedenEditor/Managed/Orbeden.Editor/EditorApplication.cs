using System;
using System.Runtime.InteropServices;

namespace OrbedenEditor;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct EditorApplicationNativeApi
{
    public IntPtr Context;
    public delegate* unmanaged[Cdecl]<IntPtr, void> RequestRepaint;
}
#pragma warning restore CS0649

/// <summary>Editor 应用级原生操作入口。</summary>
public static unsafe class EditorApplication
{
    private static EditorApplicationNativeApi api;

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
}
