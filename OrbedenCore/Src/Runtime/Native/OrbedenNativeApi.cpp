#include "Runtime/Native/OrbedenNativeApi.h"

OrbedenEngineNativeApi OrbedenEngineNativeApi::Create()
{
    OrbedenEngineNativeApi api;
    api.World = WorldBind::Create();
    api.PathDefines = PathDefinesBind::Create();
    api.Ens = EnsBind::Create();
    api.TransformComponent = TransformComponentBind::Create();
    api.StaticMeshRenderer = StaticMeshRendererBind::Create();
    api.Object = ObjectBind::Create();
    api.Mesh = MeshBind::Create();
    api.Material = MaterialBind::Create();
    api.Shader = ShaderBind::Create();
    api.RigidBody = RigidBodyBind::Create();
    api.Collider = ColliderBind::Create();
    api.CharacterController = CharacterControllerBind::Create();
    api.ObjectExtension = ObjectExtensionBind::Create();
    return api;
}

OrbedenNativeApi OrbedenNativeApi::Create()
{
    OrbedenNativeApi api;
    api.Gui = RuntimeGuiBridge::GetApi();
    api.World = WorldBind::Create();
    api.PathDefines = PathDefinesBind::Create();
    api.Ens = EnsBind::Create();
    api.TransformComponent = TransformComponentBind::Create();
    api.StaticMeshRenderer = StaticMeshRendererBind::Create();
    api.Object = ObjectBind::Create();
    api.Mesh = MeshBind::Create();
    api.Material = MaterialBind::Create();
    api.Shader = ShaderBind::Create();
    api.RigidBody = RigidBodyBind::Create();
    api.Collider = ColliderBind::Create();
    api.CharacterController = CharacterControllerBind::Create();
    api.GuiExtension = RuntimeGuiBridge::GetExtensionApi();
    api.GuiAdvanced = RuntimeGuiBridge::GetAdvancedApi();
    api.ObjectExtension = ObjectExtensionBind::Create();
    return api;
}
