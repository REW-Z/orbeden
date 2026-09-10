#include "Runtime/Object/Collider.h"

OBJECT_TYPE_IMPLEMENT_ABSTRACT(Collider, Component)
OBJECT_TYPE_IMPLEMENT(BoxCollider, Collider)
OBJECT_TYPE_IMPLEMENT(SphereCollider, Collider)
OBJECT_TYPE_IMPLEMENT(CapsuleCollider, Collider)
OBJECT_TYPE_IMPLEMENT(ConvexMeshCollider, Collider)
OBJECT_TYPE_IMPLEMENT(TriangleMeshCollider, Collider)

//获取盒形几何类型。
ColliderGeometryType BoxCollider::GetGeometryType() const
{
    return ColliderGeometryType::Box;
}

//获取球形几何类型。
ColliderGeometryType SphereCollider::GetGeometryType() const
{
    return ColliderGeometryType::Sphere;
}

//获取胶囊形几何类型。
ColliderGeometryType CapsuleCollider::GetGeometryType() const
{
    return ColliderGeometryType::Capsule;
}

//获取凸包网格几何类型。
ColliderGeometryType ConvexMeshCollider::GetGeometryType() const
{
    return ColliderGeometryType::ConvexMesh;
}

//卸载时释放凸包网格软引用。
void ConvexMeshCollider::OnDetach()
{
    mesh.SetInstanceId(StringId());
}

//获取三角网格几何类型。
ColliderGeometryType TriangleMeshCollider::GetGeometryType() const
{
    return ColliderGeometryType::TriangleMesh;
}

//卸载时释放三角网格软引用。
void TriangleMeshCollider::OnDetach()
{
    mesh.SetInstanceId(StringId());
}
