using System;
using System.Runtime.InteropServices;

namespace OrbedenCore.CSharp;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct OrbedenNativeApi
{
    public RuntimeGuiApi Gui;
    public WorldBindApi World;
    public PathDefinesBindApi PathDefines;
    public EnsBindApi Ens;
    public SpaceComponentBindApi SpaceComponent;
    public StaticMeshRendererBindApi StaticMeshRenderer;
    public MeshBindApi Mesh;
    public MaterialBindApi Material;
    public ShaderBindApi Shader;
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
            PathDefinesBind.Initialize(default);
            EnsBind.Initialize(default);
            SpaceComponentBind.Initialize(default);
            StaticMeshRendererBind.Initialize(default);
            MeshBind.Initialize(default);
            MaterialBind.Initialize(default);
            ShaderBind.Initialize(default);
            return;
        }

        OrbedenNativeApi api = *(OrbedenNativeApi*)nativeApi;
        NativeGui.Initialize(api.Gui);
        WorldBind.Initialize(api.World);
        PathDefinesBind.Initialize(api.PathDefines);
        EnsBind.Initialize(api.Ens);
        SpaceComponentBind.Initialize(api.SpaceComponent);
        StaticMeshRendererBind.Initialize(api.StaticMeshRenderer);
        MeshBind.Initialize(api.Mesh);
        MaterialBind.Initialize(api.Material);
        ShaderBind.Initialize(api.Shader);
    }
}
