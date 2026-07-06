using System;
using System.Runtime.InteropServices;

namespace OrbedenCore.CSharp;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct OrbedenNativeApi
{
    public RuntimeGuiApi Gui;
    public WorldBindApi World;
    public EnsBindApi Ens;
    public SpaceComponentBindApi SpaceComponent;
    public StaticMeshRendererBindApi StaticMeshRenderer;
}
#pragma warning restore CS0649

/// <summary>AOT 运行时原生 API 初始化入口。</summary>
public static unsafe class OrbedenCoreRuntime
{
    /// <summary>初始化引擎原生 API。</summary>
    public static void Initialize(IntPtr nativeApi)
    {
        if (nativeApi == IntPtr.Zero)
        {
            NativeGui.Initialize(default);
            WorldBind.Initialize(default);
            EnsBind.Initialize(default);
            SpaceComponentBind.Initialize(default);
            StaticMeshRendererBind.Initialize(default);
            return;
        }

        OrbedenNativeApi api = *(OrbedenNativeApi*)nativeApi;
        NativeGui.Initialize(api.Gui);
        WorldBind.Initialize(api.World);
        EnsBind.Initialize(api.Ens);
        SpaceComponentBind.Initialize(api.SpaceComponent);
        StaticMeshRendererBind.Initialize(api.StaticMeshRenderer);
    }
}
