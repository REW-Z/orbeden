#pragma once

#include "Physics/PhysicsTypes.h"
#include "Runtime/Object/Component.h"

//由 PhysicsSystem 驱动的刚体组件
class RigidBody : public Component
{
    OBJECT_TYPE_DECLARE(RigidBody)
    ORBEDEN_COMPONENT_UNIQUE

private:
    ORBEDEN_BIND_ACCESSORS(GetEnabled, SetEnabled)
    bool enabled = true;
    ORBEDEN_BIND_ACCESSORS(GetBodyType, SetBodyType)
    PhysicsBodyType bodyType = PhysicsBodyType::Dynamic;
    ORBEDEN_BIND_ACCESSORS(GetMass, SetMass)
    float32 mass = 1.0f;
    ORBEDEN_BIND_ACCESSORS(GetAutomaticCenterOfMass, SetAutomaticCenterOfMass)
    bool automaticCenterOfMass = true;
    ORBEDEN_BIND_ACCESSORS(GetCenterOfMass, SetCenterOfMass)
    vector3 centerOfMass = { 0.0f, 0.0f, 0.0f };
    ORBEDEN_BIND_ACCESSORS(GetAutomaticInertiaTensor, SetAutomaticInertiaTensor)
    bool automaticInertiaTensor = true;
    ORBEDEN_BIND_ACCESSORS(GetInertiaTensor, SetInertiaTensor)
    vector3 inertiaTensor = { 1.0f, 1.0f, 1.0f };
    ORBEDEN_BIND_ACCESSORS(GetInertiaTensorRotation, SetInertiaTensorRotation)
    quaternion inertiaTensorRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    ORBEDEN_BIND_ACCESSORS(GetUseGravity, SetUseGravity)
    bool useGravity = true;
    ORBEDEN_BIND_ACCESSORS(GetLinearDamping, SetLinearDamping)
    float32 linearDamping = 0.05f;
    ORBEDEN_BIND_ACCESSORS(GetAngularDamping, SetAngularDamping)
    float32 angularDamping = 0.05f;
    ORBEDEN_BIND_ACCESSORS(GetLinearVelocity, SetLinearVelocity)
    vector3 linearVelocity;
    ORBEDEN_BIND_ACCESSORS(GetAngularVelocity, SetAngularVelocity)
    vector3 angularVelocity;
    ORBEDEN_BIND_ACCESSORS(GetContinuousCollisionDetection, SetContinuousCollisionDetection)
    bool continuousCollisionDetection = false;
    ORBEDEN_BIND_ACCESSORS(GetLockFlags, SetLockFlags)
    uint32 lockFlags = PhysicsLockNone;

public:
    //读取 enabled
    ORBEDEN_BIND_PROPERTY(enabled)
    bool GetEnabled() const;
    //设置 enabled 并登记物理更新
    ORBEDEN_BIND_PROPERTY(enabled)
    void SetEnabled(bool value);

    //读取 bodyType
    ORBEDEN_BIND_PROPERTY(bodyType)
    PhysicsBodyType GetBodyType() const;
    //设置 bodyType 并登记物理更新
    ORBEDEN_BIND_PROPERTY(bodyType)
    void SetBodyType(PhysicsBodyType value);

    //读取 mass
    ORBEDEN_BIND_PROPERTY(mass)
    float32 GetMass() const;
    //设置 mass 并登记物理更新
    ORBEDEN_BIND_PROPERTY(mass)
    void SetMass(float32 value);

    //读取 automaticCenterOfMass
    ORBEDEN_BIND_PROPERTY(automaticCenterOfMass)
    bool GetAutomaticCenterOfMass() const;
    //设置 automaticCenterOfMass 并登记物理更新
    ORBEDEN_BIND_PROPERTY(automaticCenterOfMass)
    void SetAutomaticCenterOfMass(bool value);

    //读取 centerOfMass
    ORBEDEN_BIND_PROPERTY(centerOfMass)
    const vector3& GetCenterOfMass() const;
    //设置 centerOfMass 并登记物理更新
    ORBEDEN_BIND_PROPERTY(centerOfMass)
    void SetCenterOfMass(const vector3& value);

    //读取 automaticInertiaTensor
    ORBEDEN_BIND_PROPERTY(automaticInertiaTensor)
    bool GetAutomaticInertiaTensor() const;
    //设置 automaticInertiaTensor 并登记物理更新
    ORBEDEN_BIND_PROPERTY(automaticInertiaTensor)
    void SetAutomaticInertiaTensor(bool value);

    //读取 inertiaTensor
    ORBEDEN_BIND_PROPERTY(inertiaTensor)
    const vector3& GetInertiaTensor() const;
    //设置 inertiaTensor 并登记物理更新
    ORBEDEN_BIND_PROPERTY(inertiaTensor)
    void SetInertiaTensor(const vector3& value);

    //读取 inertiaTensorRotation
    ORBEDEN_BIND_PROPERTY(inertiaTensorRotation)
    const quaternion& GetInertiaTensorRotation() const;
    //设置 inertiaTensorRotation 并登记物理更新
    ORBEDEN_BIND_PROPERTY(inertiaTensorRotation)
    void SetInertiaTensorRotation(const quaternion& value);

    //读取 useGravity
    ORBEDEN_BIND_PROPERTY(useGravity)
    bool GetUseGravity() const;
    //设置 useGravity 并登记物理更新
    ORBEDEN_BIND_PROPERTY(useGravity)
    void SetUseGravity(bool value);

    //读取 linearDamping
    ORBEDEN_BIND_PROPERTY(linearDamping)
    float32 GetLinearDamping() const;
    //设置 linearDamping 并登记物理更新
    ORBEDEN_BIND_PROPERTY(linearDamping)
    void SetLinearDamping(float32 value);

    //读取 angularDamping
    ORBEDEN_BIND_PROPERTY(angularDamping)
    float32 GetAngularDamping() const;
    //设置 angularDamping 并登记物理更新
    ORBEDEN_BIND_PROPERTY(angularDamping)
    void SetAngularDamping(float32 value);

    //读取 linearVelocity
    ORBEDEN_BIND_PROPERTY(linearVelocity)
    const vector3& GetLinearVelocity() const;
    //设置 linearVelocity 并登记物理更新
    ORBEDEN_BIND_PROPERTY(linearVelocity)
    void SetLinearVelocity(const vector3& value);

    //读取 angularVelocity
    ORBEDEN_BIND_PROPERTY(angularVelocity)
    const vector3& GetAngularVelocity() const;
    //设置 angularVelocity 并登记物理更新
    ORBEDEN_BIND_PROPERTY(angularVelocity)
    void SetAngularVelocity(const vector3& value);

    //读取 continuousCollisionDetection
    ORBEDEN_BIND_PROPERTY(continuousCollisionDetection)
    bool GetContinuousCollisionDetection() const;
    //设置 continuousCollisionDetection 并登记物理更新
    ORBEDEN_BIND_PROPERTY(continuousCollisionDetection)
    void SetContinuousCollisionDetection(bool value);

    //读取 lockFlags
    ORBEDEN_BIND_PROPERTY(lockFlags)
    uint32 GetLockFlags() const;
    //设置 lockFlags 并登记物理更新
    ORBEDEN_BIND_PROPERTY(lockFlags)
    void SetLockFlags(uint32 value);

    //回写物理速度并保持输入队列不变
    ORBEDEN_BIND_IGNORE
    void WritePhysicsVelocities(const vector3& linear, const vector3& angular);

    //累积本帧待施加的力，PhysicsSystem 在下一物理步施加后清零。
    //瞬态数据：不参与序列化、托管绑定和 body 重建哈希。
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
