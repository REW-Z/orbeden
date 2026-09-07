#include "Physics/RigidBodyComponent.h"

#include "Runtime/Ens.h"
#include "Runtime/Object/TransformComponent.h"

OBJECT_TYPE_IMPLEMENT(RigidBodyComponent, Component)

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
void RigidBodyComponent::AddForce(const vector3& force)
{
    pendingForce.x += force.x;
    pendingForce.y += force.y;
    pendingForce.z += force.z;
}

//累积一个绕质心的力矩
void RigidBodyComponent::AddTorque(const vector3& torque)
{
    pendingTorque.x += torque.x;
    pendingTorque.y += torque.y;
    pendingTorque.z += torque.z;
}

//累积一个作用于世界空间位置的力
void RigidBodyComponent::AddForceAtPosition(const vector3& force, const vector3& worldPosition)
{
    AddForce(force);

    Ens* ens = GetEns();
    vector3 center = ens && ens->Transform() ? ens->Transform()->worldPosition : vector3();
    vector3 offset = { worldPosition.x - center.x, worldPosition.y - center.y, worldPosition.z - center.z };
    vector3 torque = Cross(offset, force);
    AddTorque(torque);
}
