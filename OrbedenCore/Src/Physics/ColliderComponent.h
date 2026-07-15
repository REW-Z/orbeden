#pragma once

#include "Physics/PhysicsTypes.h"
#include "Runtime/Object/Mesh.h"

//单一几何碰撞体；没有刚体组件时作为静态碰撞体
class ColliderComponent : public Component
{
    OBJECT_TYPE_DECLARE(ColliderComponent)

public:
    bool enabled = true;
    ColliderShape shape = ColliderShape::Box;
    bool isTrigger = false;
    vector3 center;
    vector3 halfExtents = { 0.5f, 0.5f, 0.5f };
    float32 radius = 0.5f;
    float32 halfHeight = 0.5f;
    Ref<Mesh> mesh;
    float32 staticFriction = 0.5f;
    float32 dynamicFriction = 0.5f;
    float32 restitution = 0.0f;
    uint32 collisionLayer = 1u;
    uint32 collisionMask = 0xFFFFFFFFu;

    //卸载时释放网格软引用
    void OnDetach() override;
};
