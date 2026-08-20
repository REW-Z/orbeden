#include "Physics/ColliderComponent.h"

OBJECT_TYPE_IMPLEMENT_ABSTRACT(ColliderComponent, Component)
OBJECT_TYPE_IMPLEMENT(BoxColliderComponent, ColliderComponent)
OBJECT_TYPE_IMPLEMENT(SphereColliderComponent, ColliderComponent)
OBJECT_TYPE_IMPLEMENT(CapsuleColliderComponent, ColliderComponent)
OBJECT_TYPE_IMPLEMENT(ConvexMeshColliderComponent, ColliderComponent)
OBJECT_TYPE_IMPLEMENT(TriangleMeshColliderComponent, ColliderComponent)

//获取盒形几何类型。
ColliderGeometryType BoxColliderComponent::GetGeometryType() const
{
    return ColliderGeometryType::Box;
}

//获取球形几何类型。
ColliderGeometryType SphereColliderComponent::GetGeometryType() const
{
    return ColliderGeometryType::Sphere;
}

//获取胶囊形几何类型。
ColliderGeometryType CapsuleColliderComponent::GetGeometryType() const
{
    return ColliderGeometryType::Capsule;
}

//获取凸包网格几何类型。
ColliderGeometryType ConvexMeshColliderComponent::GetGeometryType() const
{
    return ColliderGeometryType::ConvexMesh;
}

//卸载时释放凸包网格软引用。
void ConvexMeshColliderComponent::OnDetach()
{
    mesh.SetInstanceId(StringId());
}

//获取三角网格几何类型。
ColliderGeometryType TriangleMeshColliderComponent::GetGeometryType() const
{
    return ColliderGeometryType::TriangleMesh;
}

//卸载时释放三角网格软引用。
void TriangleMeshColliderComponent::OnDetach()
{
    mesh.SetInstanceId(StringId());
}
