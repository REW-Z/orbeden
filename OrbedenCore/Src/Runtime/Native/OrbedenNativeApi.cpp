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
    api.Mesh = MeshBind::Create();
    api.Material = MaterialBind::Create();
    api.Shader = ShaderBind::Create();
    return api;
}
