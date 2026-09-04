using System;
using System.Runtime.InteropServices;

namespace Orbeden;

#pragma warning disable CS0649
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct OrbedenEngineNativeApi
{
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
    public ObjectExtensionBindApi ObjectExtension;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
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
    public NativeScriptInteropApi ScriptInterop;
    public ScriptBehaviourBindApi ScriptBehaviour;
}
#pragma warning restore CS0649

/// <summary>AOT 运行时原生 API 初始化入口。</summary>
public static unsafe class OrbedenCoreRuntime
{
    private static bool nativeAbiValidated;

    /// <summary>初始化引擎原生 API。</summary>
    public static void Initialize(IntPtr nativeApi)
    {
        ValidateNativeApiLayout();

        if (nativeApi == IntPtr.Zero)
        {
            InitializeEngineBindings(default(OrbedenNativeApi));
            ScriptInteropDispatch.Initialize(default);
            ScriptBehaviour.InitializeNativeApi(default);
            GUI.InitializeNativeApi(default, default, default);
            return;
        }

        OrbedenNativeApi api = *(OrbedenNativeApi*)nativeApi;
        InitializeEngineBindings(api);
        ScriptInteropDispatch.Initialize(api.ScriptInterop);
        ScriptBehaviour.InitializeNativeApi(api.ScriptBehaviour);
        GUI.InitializeNativeApi(api.Gui, api.GuiExtension, api.GuiAdvanced);
    }

    /// <summary>初始化引擎对象和组件绑定。</summary>
    public static void InitializeEngineBindings(IntPtr nativeApi)
    {
        ValidateNativeApiLayout();

        if (nativeApi == IntPtr.Zero)
        {
            Ens.InitializeWorldNativeApi(default);
            PathDefines.InitializeNativeApi(default);
            Ens.InitializeEnsNativeApi(default);
            TransformComponent.InitializeNativeApi(default);
            StaticMeshRenderer.InitializeNativeApi(default);
            Object.InitializeNativeApi(default, default);
            Mesh.InitializeNativeApi(default);
            Material.InitializeNativeApi(default);
            Shader.InitializeNativeApi(default);
            RigidBody.InitializeNativeApi(default);
            Collider.InitializeNativeApi(default);
            CharacterController.InitializeNativeApi(default);
            return;
        }

        OrbedenEngineNativeApi api = *(OrbedenEngineNativeApi*)nativeApi;
        InitializeEngineBindings(api);
    }

    /// <summary>初始化 Editor 使用的引擎对象和组件绑定。</summary>
    private static void InitializeEngineBindings(OrbedenEngineNativeApi api)
    {
        Ens.InitializeWorldNativeApi(api.World);
        PathDefines.InitializeNativeApi(api.PathDefines);
        Ens.InitializeEnsNativeApi(api.Ens);
        TransformComponent.InitializeNativeApi(api.TransformComponent);
        StaticMeshRenderer.InitializeNativeApi(api.StaticMeshRenderer);
        Object.InitializeNativeApi(api.Object, api.ObjectExtension);
        Mesh.InitializeNativeApi(api.Mesh);
        Material.InitializeNativeApi(api.Material);
        Shader.InitializeNativeApi(api.Shader);
        RigidBody.InitializeNativeApi(api.RigidBody);
        Collider.InitializeNativeApi(api.Collider);
        CharacterController.InitializeNativeApi(api.CharacterController);
    }

    /// <summary>初始化 Game 使用的引擎对象和组件绑定。</summary>
    private static void InitializeEngineBindings(OrbedenNativeApi api)
    {
        Ens.InitializeWorldNativeApi(api.World);
        PathDefines.InitializeNativeApi(api.PathDefines);
        Ens.InitializeEnsNativeApi(api.Ens);
        TransformComponent.InitializeNativeApi(api.TransformComponent);
        StaticMeshRenderer.InitializeNativeApi(api.StaticMeshRenderer);
        Object.InitializeNativeApi(api.Object, api.ObjectExtension);
        Mesh.InitializeNativeApi(api.Mesh);
        Material.InitializeNativeApi(api.Material);
        Shader.InitializeNativeApi(api.Shader);
        RigidBody.InitializeNativeApi(api.RigidBody);
        Collider.InitializeNativeApi(api.Collider);
        CharacterController.InitializeNativeApi(api.CharacterController);
    }

    //在读取 C++ 函数表前验证托管 ABI 的固定尺寸。
    private static void ValidateNativeApiLayout()
    {
        if (nativeAbiValidated) return;

        ValidateSize<EnsId>(nameof(EnsId), 8);
        ValidateSize<vector2>(nameof(vector2), 8);
        ValidateSize<vector3>(nameof(vector3), 12);
        ValidateSize<quaternion>(nameof(quaternion), 16);
        ValidateSize<color4>(nameof(color4), 16);

        ValidateFunctionTable<WorldBindApi>(nameof(WorldBindApi), 4);
        ValidateFunctionTable<PathDefinesBindApi>(nameof(PathDefinesBindApi), 2);
        ValidateFunctionTable<EnsBindApi>(nameof(EnsBindApi), 11);
        ValidateFunctionTable<TransformComponentBindApi>(nameof(TransformComponentBindApi), 10);
        ValidateFunctionTable<StaticMeshRendererBindApi>(nameof(StaticMeshRendererBindApi), 10);
        ValidateFunctionTable<ObjectBindApi>(nameof(ObjectBindApi), 6);
        ValidateFunctionTable<ObjectExtensionBindApi>(nameof(ObjectExtensionBindApi), 1);
        ValidateFunctionTable<MeshBindApi>(nameof(MeshBindApi), 28);
        ValidateFunctionTable<MaterialBindApi>(nameof(MaterialBindApi), 20);
        ValidateFunctionTable<ShaderBindApi>(nameof(ShaderBindApi), 27);
        ValidateFunctionTable<RigidBodyBindApi>(nameof(RigidBodyBindApi), 23);
        ValidateFunctionTable<ColliderBindApi>(nameof(ColliderBindApi), 32);
        ValidateFunctionTable<CharacterControllerBindApi>(nameof(CharacterControllerBindApi), 25);
        ValidateFunctionTable<RuntimeGuiApi>(nameof(RuntimeGuiApi), 11);
        ValidateFunctionTable<RuntimeGuiExtensionApi>(nameof(RuntimeGuiExtensionApi), 4);
        ValidateFunctionTable<RuntimeGuiAdvancedApi>(nameof(RuntimeGuiAdvancedApi), 17);
        ValidateFunctionTable<NativeScriptInteropApi>(nameof(NativeScriptInteropApi), 9);
        ValidateFunctionTable<ManagedScriptInteropApi>(nameof(ManagedScriptInteropApi), 11);
        ValidateFunctionTable<ScriptBehaviourBindApi>(nameof(ScriptBehaviourBindApi), 15);
        ValidateFunctionTable<OrbedenEngineNativeApi>(nameof(OrbedenEngineNativeApi), 199);
        ValidateFunctionTable<OrbedenNativeApi>(nameof(OrbedenNativeApi), 255);

        nativeAbiValidated = true;
    }

    //验证一个可跨语言按值复制的 ABI 结构尺寸。
    private static void ValidateSize<T>(string name, int expectedSize) where T : unmanaged
    {
        if (sizeof(T) != expectedSize)
            throw new TypeLoadException($"{name} ABI size mismatch: expected {expectedSize}, actual {sizeof(T)}.");
    }

    //验证全由函数指针槽组成的函数表尺寸。
    private static void ValidateFunctionTable<T>(string name, int slotCount) where T : unmanaged
    {
        ValidateSize<T>(name, checked(slotCount * IntPtr.Size));
    }
}
