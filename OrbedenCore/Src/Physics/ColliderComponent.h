#pragma once

#include "Defines/types.h"
#include "Runtime/Object/Mesh.h"

//碰撞体几何类型，仅用于原生物理和托管包装分派。
enum class ColliderGeometryType : uint32
{
    Box = 0,
    Sphere = 1,
    Capsule = 2,
    ConvexMesh = 3,
    TriangleMesh = 4,
};

//所有物理碰撞体的抽象基类；没有刚体组件时作为静态碰撞体。
class ColliderComponent : public Component
{
    OBJECT_TYPE_DECLARE_ABSTRACT(ColliderComponent)

public:
    bool enabled = true;
    bool isTrigger = false;
    vector3 center;
    float32 staticFriction = 0.5f;
    float32 dynamicFriction = 0.5f;
    float32 restitution = 0.0f;
    uint32 collisionLayer = 1u;
    uint32 collisionMask = 0xFFFFFFFFu;

    //获取具体几何类型。
    virtual ColliderGeometryType GetGeometryType() const = 0;
};

//盒形碰撞体。
class BoxColliderComponent final : public ColliderComponent
{
    OBJECT_TYPE_DECLARE(BoxColliderComponent)

public:
    vector3 halfExtents = { 0.5f, 0.5f, 0.5f };

    ColliderGeometryType GetGeometryType() const override;
};

//球形碰撞体。
class SphereColliderComponent final : public ColliderComponent
{
    OBJECT_TYPE_DECLARE(SphereColliderComponent)

public:
    float32 radius = 0.5f;

    ColliderGeometryType GetGeometryType() const override;
};

//胶囊形碰撞体。
class CapsuleColliderComponent final : public ColliderComponent
{
    OBJECT_TYPE_DECLARE(CapsuleColliderComponent)

public:
    float32 radius = 0.5f;
    float32 halfHeight = 0.5f;

    ColliderGeometryType GetGeometryType() const override;
};

//凸包网格碰撞体。
class ConvexMeshColliderComponent final : public ColliderComponent
{
    OBJECT_TYPE_DECLARE(ConvexMeshColliderComponent)

public:
    Ref<Mesh> mesh;

    ColliderGeometryType GetGeometryType() const override;
    void OnDetach() override;
};

//三角网格碰撞体。
class TriangleMeshColliderComponent final : public ColliderComponent
{
    OBJECT_TYPE_DECLARE(TriangleMeshColliderComponent)

public:
    Ref<Mesh> mesh;

    ColliderGeometryType GetGeometryType() const override;
    void OnDetach() override;
};
