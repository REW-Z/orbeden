#include "Runtime/Object/Collider.h"
#include "Physics/PhysicsSystem.h"
#include "PxPhysicsAPI.h"
#include <stdexcept>

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
    DiscardPreparedMesh();
    mesh.SetInstanceId(StringId());
}

//后台烘焙当前网格快照
bool TriangleMeshCollider::PrepareMeshAsync()
{
    PhysicsSystem* physics = PhysicsSystem::Current();
    Mesh* source = mesh.Get();
    if (!source || !physics || !physics->IsInitialized() || (cooking && !cooking->IsFinished()))
    {
        return false;
    }
    auto positions = source->vertices;
    auto indices = source->indices;
    auto bytes = std::make_shared<List<uint8>>();
    auto params = *physics->GetCookingParams();
    cookingVersion = source->GetContentVersion();
    cookingMeshId = source->GetObjectId();
    cookedBytes = bytes;
    cooking = std::make_shared<ThreadOperation>([positions = std::move(positions), indices = std::move(indices), bytes, params]
    {
        physx::PxTriangleMeshDesc description;
        description.points.count = static_cast<physx::PxU32>(positions.size());
        description.points.stride = sizeof(vector3);
        description.points.data = positions.data();
        description.triangles.count = static_cast<physx::PxU32>(indices.size() / 3);
        description.triangles.stride = sizeof(uint32) * 3;
        description.triangles.data = indices.data();
        physx::PxDefaultMemoryOutputStream output;
        if (!PxCookTriangleMesh(params, description, output))
        {
            throw std::runtime_error("Triangle mesh cooking failed.");
        }
        bytes->assign(output.getData(), output.getData() + output.getSize());
    });
    return true;
}

//丢弃当前烘焙结果并取消排队任务
void TriangleMeshCollider::DiscardPreparedMesh()
{
    if (cooking)
    {
        cooking->Cancel();
    }
    cooking.reset();
    cookedBytes.reset();
    cookingVersion = 0;
    cookingMeshId = 0;
}

//提交后台结果，返回等待、成功或失败
int32 TriangleMeshCollider::ApplyPreparedMesh()
{
    if (!cooking || !cooking->IsFinished())
    {
        return 0;
    }
    Mesh* source = mesh.Get();
    bool success = cooking->Succeeded() && source && source->GetObjectId() == cookingMeshId
        && source->GetContentVersion() == cookingVersion;
    if (success)
    {
        success = PhysicsSystem::Current()->InstallTriangleMesh(*source, *cookedBytes);
    }
    cooking.reset();
    cookedBytes.reset();
    return success ? 1 : -1;
}

//登记碰撞属性变化
void Collider::NotifyPhysicsChanged()
{
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 enabled
bool Collider::GetEnabled() const
{
    return enabled;
}

//设置 enabled 并登记物理更新
void Collider::SetEnabled(bool value)
{
    if (enabled == value)
    {
        return;
    }
    enabled = value;
    NotifyPhysicsChanged();
}

//读取 isTrigger
bool Collider::GetIsTrigger() const
{
    return isTrigger;
}

//设置 isTrigger 并登记物理更新
void Collider::SetIsTrigger(bool value)
{
    if (isTrigger == value)
    {
        return;
    }
    isTrigger = value;
    NotifyPhysicsChanged();
}

//读取 center
const vector3& Collider::GetCenter() const
{
    return center;
}

//设置 center 并登记物理更新
void Collider::SetCenter(const vector3& value)
{
    if (center.x == value.x && center.y == value.y && center.z == value.z)
    {
        return;
    }
    center = value;
    NotifyPhysicsChanged();
}

//读取 staticFriction
float32 Collider::GetStaticFriction() const
{
    return staticFriction;
}

//设置 staticFriction 并登记物理更新
void Collider::SetStaticFriction(float32 value)
{
    if (staticFriction == value)
    {
        return;
    }
    staticFriction = value;
    NotifyPhysicsChanged();
}

//读取 dynamicFriction
float32 Collider::GetDynamicFriction() const
{
    return dynamicFriction;
}

//设置 dynamicFriction 并登记物理更新
void Collider::SetDynamicFriction(float32 value)
{
    if (dynamicFriction == value)
    {
        return;
    }
    dynamicFriction = value;
    NotifyPhysicsChanged();
}

//读取 restitution
float32 Collider::GetRestitution() const
{
    return restitution;
}

//设置 restitution 并登记物理更新
void Collider::SetRestitution(float32 value)
{
    if (restitution == value)
    {
        return;
    }
    restitution = value;
    NotifyPhysicsChanged();
}

//读取 halfExtents
const vector3& BoxCollider::GetHalfExtents() const
{
    return halfExtents;
}

//设置 halfExtents 并登记物理更新
void BoxCollider::SetHalfExtents(const vector3& value)
{
    if (halfExtents.x == value.x && halfExtents.y == value.y && halfExtents.z == value.z)
    {
        return;
    }
    halfExtents = value;
    NotifyPhysicsChanged();
}

//读取 radius
float32 SphereCollider::GetRadius() const
{
    return radius;
}

//设置 radius 并登记物理更新
void SphereCollider::SetRadius(float32 value)
{
    if (radius == value)
    {
        return;
    }
    radius = value;
    NotifyPhysicsChanged();
}

//读取 radius
float32 CapsuleCollider::GetRadius() const
{
    return radius;
}

//设置 radius 并登记物理更新
void CapsuleCollider::SetRadius(float32 value)
{
    if (radius == value)
    {
        return;
    }
    radius = value;
    NotifyPhysicsChanged();
}

//读取 halfHeight
float32 CapsuleCollider::GetHalfHeight() const
{
    return halfHeight;
}

//设置 halfHeight 并登记物理更新
void CapsuleCollider::SetHalfHeight(float32 value)
{
    if (halfHeight == value)
    {
        return;
    }
    halfHeight = value;
    NotifyPhysicsChanged();
}

//读取 mesh
const Ref<Mesh>& ConvexMeshCollider::GetMesh() const
{
    return mesh;
}

//设置 mesh 并登记物理更新
void ConvexMeshCollider::SetMesh(const Ref<Mesh>& value)
{
    if (mesh.GetInstanceId() == value.GetInstanceId())
    {
        return;
    }
    mesh = value;
    NotifyPhysicsChanged();
}

//读取 mesh
const Ref<Mesh>& TriangleMeshCollider::GetMesh() const
{
    return mesh;
}

//设置 mesh 并登记物理更新
void TriangleMeshCollider::SetMesh(const Ref<Mesh>& value)
{
    if (mesh.GetInstanceId() == value.GetInstanceId())
    {
        return;
    }
    mesh = value;
    NotifyPhysicsChanged();
}
