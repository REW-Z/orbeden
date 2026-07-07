#include "Runtime/Object/StaticMeshRenderer.h"

#include "Runtime/ResourceManager.h"

OBJECT_TYPE_IMPLEMENT(StaticMeshRenderer, Component)

void StaticMeshRenderer::OnDetach()
{
    std::string meshKey = mesh.GetInstanceId().GetPath();
    if (!meshKey.empty())
    {
        ResourceManager::ReleaseWorldRef(meshKey);
    }

    mesh.SetInstanceId(StringId());
}
