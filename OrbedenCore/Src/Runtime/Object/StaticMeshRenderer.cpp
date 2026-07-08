#include "Runtime/Object/StaticMeshRenderer.h"

OBJECT_TYPE_IMPLEMENT(StaticMeshRenderer, Component)

void StaticMeshRenderer::OnDetach()
{
    mesh.SetInstanceId(StringId());
}
