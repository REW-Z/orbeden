#pragma once

#include "Physics/PhysicsTypes.h"
#include "Runtime/Object/Component.h"

//由 PhysicsSystem 驱动的刚体组件
class RigidBody : public Component
{
    OBJECT_TYPE_DECLARE(RigidBody)
    ORBEDEN_COMPONENT_UNIQUE

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

    //累积本帧待施加的力，PhysicsSystem 在下一物理步施加后清零。
    //瞬态数据：不参与序列化（字段注册由 PhysicsReflection 手工表覆盖）和 body 重建哈希。
    ORBEDEN_BIND_IGNORE
    vector3 pendingForce;
    ORBEDEN_BIND_IGNORE
    vector3 pendingTorque;

    //累积一个作用于质心的力。
    void AddForce(const vector3& force);

    //累积一个绕质心的力矩。
    void AddTorque(const vector3& torque);

    //累积一个作用于世界空间位置的力（自动换算为力与力矩）。
    void AddForceAtPosition(const vector3& force, const vector3& worldPosition);
};
