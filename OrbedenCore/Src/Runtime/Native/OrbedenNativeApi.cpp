#include "Runtime/Native/OrbedenNativeApi.h"

OrbedenNativeApi OrbedenNativeApi::Create()
{
    OrbedenNativeApi api;
    api.Gui = RuntimeGuiBridge::GetApi();
    api.World = WorldBind::Create();
    api.Ens = EnsBind::Create();
    api.SpaceComponent = SpaceComponentBind::Create();
    api.StaticMeshRenderer = StaticMeshRendererBind::Create();
    return api;
}
