#pragma once

#include "Physics/PhysicsTypes.h"

//由 PhysicsSystem 驱动的刚体组件
class RigidBodyComponent : public Component
{
    OBJECT_TYPE_DECLARE(RigidBodyComponent)

public:
    bool enabled = true;
    PhysicsBodyType bodyType = PhysicsBodyType::Dynamic;
    float32 mass = 1.0f;
    bool useGravity = true;
    float32 linearDamping = 0.05f;
    float32 angularDamping = 0.05f;
    vector3 linearVelocity;
    vector3 angularVelocity;
    bool continuousCollisionDetection = false;
    uint32 lockFlags = PhysicsLockNone;
};
