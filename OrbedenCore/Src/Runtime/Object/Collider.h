#pragma once

#include "Defines/types.h"
#include "Runtime/Coroutines/ThreadOperation.h"
#include <memory>
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Mesh.h"

struct PreparedTriangleMesh;

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

private:
    ORBEDEN_BIND_ACCESSORS(GetEnabled, SetEnabled)
    bool enabled = true;
    ORBEDEN_BIND_ACCESSORS(GetIsTrigger, SetIsTrigger)
    bool isTrigger = false;
    ORBEDEN_BIND_ACCESSORS(GetCenter, SetCenter)
    vector3 center;
    ORBEDEN_BIND_ACCESSORS(GetStaticFriction, SetStaticFriction)
    float32 staticFriction = 0.5f;
    ORBEDEN_BIND_ACCESSORS(GetDynamicFriction, SetDynamicFriction)
    float32 dynamicFriction = 0.5f;
    ORBEDEN_BIND_ACCESSORS(GetRestitution, SetRestitution)
    float32 restitution = 0.0f;

protected:
    //登记碰撞属性变化
    void NotifyPhysicsChanged();

public:
    //读取 enabled
    ORBEDEN_BIND_PROPERTY(enabled)
    bool GetEnabled() const;
    //设置 enabled 并登记物理更新
    ORBEDEN_BIND_PROPERTY(enabled)
    void SetEnabled(bool value);

    //读取 isTrigger
    ORBEDEN_BIND_PROPERTY(isTrigger)
    bool GetIsTrigger() const;
    //设置 isTrigger 并登记物理更新
    ORBEDEN_BIND_PROPERTY(isTrigger)
    void SetIsTrigger(bool value);

    //读取 center
    ORBEDEN_BIND_PROPERTY(center)
    const vector3& GetCenter() const;
    //设置 center 并登记物理更新
    ORBEDEN_BIND_PROPERTY(center)
    void SetCenter(const vector3& value);

    //读取 staticFriction
    ORBEDEN_BIND_PROPERTY(staticFriction)
    float32 GetStaticFriction() const;
    //设置 staticFriction 并登记物理更新
    ORBEDEN_BIND_PROPERTY(staticFriction)
    void SetStaticFriction(float32 value);

    //读取 dynamicFriction
    ORBEDEN_BIND_PROPERTY(dynamicFriction)
    float32 GetDynamicFriction() const;
    //设置 dynamicFriction 并登记物理更新
    ORBEDEN_BIND_PROPERTY(dynamicFriction)
    void SetDynamicFriction(float32 value);

    //读取 restitution
    ORBEDEN_BIND_PROPERTY(restitution)
    float32 GetRestitution() const;
    //设置 restitution 并登记物理更新
    ORBEDEN_BIND_PROPERTY(restitution)
    void SetRestitution(float32 value);

    //获取具体几何类型。
    virtual ColliderGeometryType GetGeometryType() const = 0;
};

//盒形碰撞体。
class BoxCollider final : public Collider
{
    OBJECT_TYPE_DECLARE(BoxCollider)

private:
    ORBEDEN_BIND_ACCESSORS(GetHalfExtents, SetHalfExtents)
    vector3 halfExtents = { 0.5f, 0.5f, 0.5f };

public:
    //读取 halfExtents
    ORBEDEN_BIND_PROPERTY(halfExtents)
    const vector3& GetHalfExtents() const;
    //设置 halfExtents 并登记物理更新
    ORBEDEN_BIND_PROPERTY(halfExtents)
    void SetHalfExtents(const vector3& value);

    ColliderGeometryType GetGeometryType() const override;
};

//球形碰撞体。
class SphereCollider final : public Collider
{
    OBJECT_TYPE_DECLARE(SphereCollider)

private:
    ORBEDEN_BIND_ACCESSORS(GetRadius, SetRadius)
    float32 radius = 0.5f;

public:
    //读取 radius
    ORBEDEN_BIND_PROPERTY(radius)
    float32 GetRadius() const;
    //设置 radius 并登记物理更新
    ORBEDEN_BIND_PROPERTY(radius)
    void SetRadius(float32 value);

    ColliderGeometryType GetGeometryType() const override;
};

//胶囊形碰撞体。
class CapsuleCollider final : public Collider
{
    OBJECT_TYPE_DECLARE(CapsuleCollider)

private:
    ORBEDEN_BIND_ACCESSORS(GetRadius, SetRadius)
    float32 radius = 0.5f;
    ORBEDEN_BIND_ACCESSORS(GetHalfHeight, SetHalfHeight)
    float32 halfHeight = 0.5f;

public:
    //读取 radius
    ORBEDEN_BIND_PROPERTY(radius)
    float32 GetRadius() const;
    //设置 radius 并登记物理更新
    ORBEDEN_BIND_PROPERTY(radius)
    void SetRadius(float32 value);

    //读取 halfHeight
    ORBEDEN_BIND_PROPERTY(halfHeight)
    float32 GetHalfHeight() const;
    //设置 halfHeight 并登记物理更新
    ORBEDEN_BIND_PROPERTY(halfHeight)
    void SetHalfHeight(float32 value);

    ColliderGeometryType GetGeometryType() const override;
};

//凸包网格碰撞体。
class ConvexMeshCollider final : public Collider
{
    OBJECT_TYPE_DECLARE(ConvexMeshCollider)

private:
    ORBEDEN_BIND_ACCESSORS(GetMesh, SetMesh)
    Ref<Mesh> mesh;

public:
    //读取 mesh
    ORBEDEN_BIND_PROPERTY(mesh)
    const Ref<Mesh>& GetMesh() const;
    //设置 mesh 并登记物理更新
    ORBEDEN_BIND_PROPERTY(mesh)
    void SetMesh(const Ref<Mesh>& value);

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
    std::shared_ptr<PreparedTriangleMesh> cookedMesh;
    ORBEDEN_BIND_IGNORE
    uint64 cookingVersion = 0;
    ORBEDEN_BIND_IGNORE
    int32 cookingMeshId = 0;

private:
    ORBEDEN_BIND_ACCESSORS(GetMesh, SetMesh)
    Ref<Mesh> mesh;

public:
    //读取 mesh
    ORBEDEN_BIND_PROPERTY(mesh)
    const Ref<Mesh>& GetMesh() const;
    //设置 mesh 并登记物理更新
    ORBEDEN_BIND_PROPERTY(mesh)
    void SetMesh(const Ref<Mesh>& value);

    //后台烘焙当前网格快照
    bool PrepareMeshAsync();

    //丢弃当前烘焙结果并取消排队任务
    void DiscardPreparedMesh();

    //提交后台结果，返回等待、成功或失败
    int32 ApplyPreparedMesh();

    ColliderGeometryType GetGeometryType() const override;
    void OnDetach() override;
};
