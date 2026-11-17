#include "Runtime/Object/RigidBody.h"
#include "Physics/PhysicsSystem.h"

#include "Runtime/Object/Ens.h"
#include "Runtime/Object/Transform.h"
#include "PxRigidBody.h"

OBJECT_TYPE_IMPLEMENT(RigidBody, Component)

namespace
{
    //三维向量叉积。
    vector3 Cross(const vector3& a, const vector3& b)
    {
        return
        {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }
}

//累积一个作用于质心的力
void RigidBody::AddForce(const vector3& force)
{
    pendingForce.x += force.x;
    pendingForce.y += force.y;
    pendingForce.z += force.z;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyBodyInput(*this);
    }
}

//累积一个绕质心的力矩
void RigidBody::AddTorque(const vector3& torque)
{
    pendingTorque.x += torque.x;
    pendingTorque.y += torque.y;
    pendingTorque.z += torque.z;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyBodyInput(*this);
    }
}

//累积一个作用于世界空间位置的力
void RigidBody::AddForceAtPosition(const vector3& force, const vector3& worldPosition)
{
    AddForce(force);

    Ens* ens = GetEns();
    vector3 center = ens && ens->Transform() ? ens->Transform()->GetWorldPosition() : vector3();
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physx::PxRigidActor* actor = physics->GetRigidActor(GetEnsId());
        physx::PxRigidBody* body = actor ? actor->is<physx::PxRigidBody>() : nullptr;
        if (body)
        {
            physx::PxVec3 position = body->getGlobalPose().transform(body->getCMassLocalPose().p);
            center = { position.x, position.y, position.z };
        }
    }
    vector3 offset = { worldPosition.x - center.x, worldPosition.y - center.y, worldPosition.z - center.z };
    vector3 torque = Cross(offset, force);
    AddTorque(torque);
}

//读取 enabled
bool RigidBody::GetEnabled() const
{
    return enabled;
}

//设置 enabled 并登记物理更新
void RigidBody::SetEnabled(bool value)
{
    if (enabled == value)
    {
        return;
    }
    enabled = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 bodyType
PhysicsBodyType RigidBody::GetBodyType() const
{
    return bodyType;
}

//设置 bodyType 并登记物理更新
void RigidBody::SetBodyType(PhysicsBodyType value)
{
    if (bodyType == value)
    {
        return;
    }
    bodyType = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 mass
float32 RigidBody::GetMass() const
{
    return mass;
}

//设置 mass 并登记物理更新
void RigidBody::SetMass(float32 value)
{
    if (mass == value)
    {
        return;
    }
    mass = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 automaticCenterOfMass
bool RigidBody::GetAutomaticCenterOfMass() const
{
    return automaticCenterOfMass;
}

//设置 automaticCenterOfMass 并登记物理更新
void RigidBody::SetAutomaticCenterOfMass(bool value)
{
    if (automaticCenterOfMass == value)
    {
        return;
    }
    automaticCenterOfMass = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 centerOfMass
const vector3& RigidBody::GetCenterOfMass() const
{
    return centerOfMass;
}

//设置 centerOfMass 并登记物理更新
void RigidBody::SetCenterOfMass(const vector3& value)
{
    if (centerOfMass.x == value.x && centerOfMass.y == value.y && centerOfMass.z == value.z)
    {
        return;
    }
    centerOfMass = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 automaticInertiaTensor
bool RigidBody::GetAutomaticInertiaTensor() const
{
    return automaticInertiaTensor;
}

//设置 automaticInertiaTensor 并登记物理更新
void RigidBody::SetAutomaticInertiaTensor(bool value)
{
    if (automaticInertiaTensor == value)
    {
        return;
    }
    automaticInertiaTensor = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 inertiaTensor
const vector3& RigidBody::GetInertiaTensor() const
{
    return inertiaTensor;
}

//设置 inertiaTensor 并登记物理更新
void RigidBody::SetInertiaTensor(const vector3& value)
{
    if (inertiaTensor.x == value.x && inertiaTensor.y == value.y && inertiaTensor.z == value.z)
    {
        return;
    }
    inertiaTensor = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 inertiaTensorRotation
const quaternion& RigidBody::GetInertiaTensorRotation() const
{
    return inertiaTensorRotation;
}

//设置 inertiaTensorRotation 并登记物理更新
void RigidBody::SetInertiaTensorRotation(const quaternion& value)
{
    if (inertiaTensorRotation.x == value.x && inertiaTensorRotation.y == value.y && inertiaTensorRotation.z == value.z && inertiaTensorRotation.w == value.w)
    {
        return;
    }
    inertiaTensorRotation = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 useGravity
bool RigidBody::GetUseGravity() const
{
    return useGravity;
}

//设置 useGravity 并登记物理更新
void RigidBody::SetUseGravity(bool value)
{
    if (useGravity == value)
    {
        return;
    }
    useGravity = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 linearDamping
float32 RigidBody::GetLinearDamping() const
{
    return linearDamping;
}

//设置 linearDamping 并登记物理更新
void RigidBody::SetLinearDamping(float32 value)
{
    if (linearDamping == value)
    {
        return;
    }
    linearDamping = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 angularDamping
float32 RigidBody::GetAngularDamping() const
{
    return angularDamping;
}

//设置 angularDamping 并登记物理更新
void RigidBody::SetAngularDamping(float32 value)
{
    if (angularDamping == value)
    {
        return;
    }
    angularDamping = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 linearVelocity
const vector3& RigidBody::GetLinearVelocity() const
{
    return linearVelocity;
}

//设置 linearVelocity 并登记物理更新
void RigidBody::SetLinearVelocity(const vector3& value)
{
    if (linearVelocity.x == value.x && linearVelocity.y == value.y && linearVelocity.z == value.z)
    {
        return;
    }
    linearVelocity = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyBodyInput(*this);
    }
}

//读取 angularVelocity
const vector3& RigidBody::GetAngularVelocity() const
{
    return angularVelocity;
}

//设置 angularVelocity 并登记物理更新
void RigidBody::SetAngularVelocity(const vector3& value)
{
    if (angularVelocity.x == value.x && angularVelocity.y == value.y && angularVelocity.z == value.z)
    {
        return;
    }
    angularVelocity = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyBodyInput(*this);
    }
}

//读取 continuousCollisionDetection
bool RigidBody::GetContinuousCollisionDetection() const
{
    return continuousCollisionDetection;
}

//设置 continuousCollisionDetection 并登记物理更新
void RigidBody::SetContinuousCollisionDetection(bool value)
{
    if (continuousCollisionDetection == value)
    {
        return;
    }
    continuousCollisionDetection = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//读取 lockFlags
uint32 RigidBody::GetLockFlags() const
{
    return lockFlags;
}

//设置 lockFlags 并登记物理更新
void RigidBody::SetLockFlags(uint32 value)
{
    if (lockFlags == value)
    {
        return;
    }
    lockFlags = value;
    if (PhysicsSystem* physics = PhysicsSystem::Current())
    {
        physics->NotifyComponentChanged(*this);
    }
}

//回写物理速度并保持输入队列不变
void RigidBody::WritePhysicsVelocities(const vector3& linear, const vector3& angular)
{
    linearVelocity = linear;
    angularVelocity = angular;
}
