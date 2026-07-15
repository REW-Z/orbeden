#include "Physics/ColliderComponent.h"

OBJECT_TYPE_IMPLEMENT(ColliderComponent, Component)

//卸载时释放网格软引用
void ColliderComponent::OnDetach()
{
    mesh.SetInstanceId(StringId());
}
