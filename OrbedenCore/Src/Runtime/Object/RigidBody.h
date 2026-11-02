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
    //根据所属实体碰撞形状自动计算重心
    bool automaticCenterOfMass = true;
    //手动重心，相对刚体原点的位置，不随 Transform 缩放
    vector3 centerOfMass = { 0.0f, 0.0f, 0.0f };
    //根据所属实体碰撞形状和当前重心自动计算惯量
    bool automaticInertiaTensor = true;
    //手动主惯量，三个分量均须为正，单位 kg·m²
    vector3 inertiaTensor = { 1.0f, 1.0f, 1.0f };
    //手动惯量主轴相对刚体局部坐标系的旋转
    quaternion inertiaTensorRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
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
