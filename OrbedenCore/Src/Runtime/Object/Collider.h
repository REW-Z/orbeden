#pragma once

#include "Defines/types.h"
#include "Runtime/Coroutines/ThreadOperation.h"
#include <memory>
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

//物理碰撞体基类；归属自身或最近祖先的刚体，未找到时作为独立静态碰撞体
//碰撞层读取所属 Ens，交互规则由项目碰撞矩阵决定
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

private:
    ORBEDEN_BIND_IGNORE
    std::shared_ptr<ThreadOperation> cooking;
    ORBEDEN_BIND_IGNORE
    std::shared_ptr<List<uint8>> cookedBytes;
    uint64 cookingVersion = 0;
    int32 cookingMeshId = 0;

public:
    //后台烘焙当前网格快照
    bool PrepareMeshAsync();

    //丢弃当前烘焙结果并取消排队任务
    void DiscardPreparedMesh();

    //提交后台结果，返回等待、成功或失败
    int32 ApplyPreparedMesh();

    Ref<Mesh> mesh;

    ColliderGeometryType GetGeometryType() const override;
    void OnDetach() override;
};
