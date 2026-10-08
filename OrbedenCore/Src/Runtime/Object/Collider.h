#pragma once

#include "Defines/types.h"
#include "Runtime/Object/Component.h"
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
class Collider : public Component
{
    OBJECT_TYPE_DECLARE_ABSTRACT(Collider)

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
class BoxCollider final : public Collider
{
    OBJECT_TYPE_DECLARE(BoxCollider)

public:
    vector3 halfExtents = { 0.5f, 0.5f, 0.5f };

    ColliderGeometryType GetGeometryType() const override;
};

//球形碰撞体。
class SphereCollider final : public Collider
{
    OBJECT_TYPE_DECLARE(SphereCollider)

public:
    float32 radius = 0.5f;

    ColliderGeometryType GetGeometryType() const override;
};

//胶囊形碰撞体。
class CapsuleCollider final : public Collider
{
    OBJECT_TYPE_DECLARE(CapsuleCollider)

public:
    float32 radius = 0.5f;
    float32 halfHeight = 0.5f;

    ColliderGeometryType GetGeometryType() const override;
};

//凸包网格碰撞体。
class ConvexMeshCollider final : public Collider
{
    OBJECT_TYPE_DECLARE(ConvexMeshCollider)

public:
    Ref<Mesh> mesh;

    ColliderGeometryType GetGeometryType() const override;
    void OnDetach() override;
};

//三角网格碰撞体。
class TriangleMeshCollider final : public Collider
{
    OBJECT_TYPE_DECLARE(TriangleMeshCollider)

public:
    Ref<Mesh> mesh;

    ColliderGeometryType GetGeometryType() const override;
    void OnDetach() override;
};
