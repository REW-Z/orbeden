using System;
using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct OrbedenNativeApi
{
    public RuntimeGuiApi Gui;
    public WorldBindApi World;
    public PathDefinesBindApi PathDefines;
    public EnsBindApi Ens;
    public TransformComponentBindApi TransformComponent;
    public StaticMeshRendererBindApi StaticMeshRenderer;
    public ObjectBindApi Object;
    public MeshBindApi Mesh;
    public MaterialBindApi Material;
    public ShaderBindApi Shader;
    public RigidBodyBindApi RigidBody;
    public ColliderBindApi Collider;
    public CharacterControllerBindApi CharacterController;
    public RuntimeGuiExtensionApi GuiExtension;
    public RuntimeGuiAdvancedApi GuiAdvanced;
    public ObjectExtensionBindApi ObjectExtension;
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
            NativeGui.Initialize(default, default, default);
            WorldBind.Initialize(default);
            PathDefinesBind.Initialize(default);
            EnsBind.Initialize(default);
            TransformComponentBind.Initialize(default);
            StaticMeshRendererBind.Initialize(default);
            ObjectBind.Initialize(default, default);
            MeshBind.Initialize(default);
            MaterialBind.Initialize(default);
            ShaderBind.Initialize(default);
            RigidBodyBind.Initialize(default);
            ColliderBind.Initialize(default);
            CharacterControllerBind.Initialize(default);
            return;
        }

        OrbedenNativeApi api = *(OrbedenNativeApi*)nativeApi;
        NativeGui.Initialize(api.Gui, api.GuiExtension, api.GuiAdvanced);
        WorldBind.Initialize(api.World);
        PathDefinesBind.Initialize(api.PathDefines);
        EnsBind.Initialize(api.Ens);
        TransformComponentBind.Initialize(api.TransformComponent);
        StaticMeshRendererBind.Initialize(api.StaticMeshRenderer);
        ObjectBind.Initialize(api.Object, api.ObjectExtension);
        MeshBind.Initialize(api.Mesh);
        MaterialBind.Initialize(api.Material);
        ShaderBind.Initialize(api.Shader);
        RigidBodyBind.Initialize(api.RigidBody);
        ColliderBind.Initialize(api.Collider);
        CharacterControllerBind.Initialize(api.CharacterController);
    }
}
