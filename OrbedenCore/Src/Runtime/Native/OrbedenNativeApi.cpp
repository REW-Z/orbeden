#include "Runtime/Native/OrbedenNativeApi.h"

OrbedenNativeApi OrbedenNativeApi::Create()
{
    OrbedenNativeApi api;
    api.Gui = RuntimeGuiBridge::GetApi();
    api.World = WorldBind::Create();
    api.PathDefines = PathDefinesBind::Create();
    api.Ens = EnsBind::Create();
    api.SpaceComponent = SpaceComponentBind::Create();
    api.StaticMeshRenderer = StaticMeshRendererBind::Create();
    api.Object = ObjectBind::Create();
    api.Mesh = MeshBind::Create();
    api.Material = MaterialBind::Create();
    api.Shader = ShaderBind::Create();
    api.RigidBody = RigidBodyBind::Create();
    api.Collider = ColliderBind::Create();
    api.CharacterController = CharacterControllerBind::Create();
    api.GuiExtension = RuntimeGuiBridge::GetExtensionApi();
    api.GuiProject = RuntimeGuiBridge::GetProjectApi();
    api.ObjectExtension = ObjectExtensionBind::Create();
    return api;
}
