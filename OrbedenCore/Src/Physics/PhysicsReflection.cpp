#include "Physics/PhysicsReflection.h"

#include "Physics/CharacterControllerComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Runtime/Reflection.h"

namespace
{
    template<typename TObject, auto Member>
    std::string GetField(Object* object)
    {
        return Reflection::ToXmlValue(static_cast<TObject*>(object)->*Member);
    }

    template<typename TObject, auto Member>
    bool SetField(Object* object, const std::string& value)
    {
        return Reflection::SetFromXmlValue(static_cast<TObject*>(object)->*Member, value);
    }

#define PHYSICS_FIELD(TYPE, MEMBER, TYPE_NAME, KIND) \
    Reflection::FieldInfo(#MEMBER, TYPE_NAME, Reflection::FieldKind::KIND, true, GetField<TYPE, &TYPE::MEMBER>, SetField<TYPE, &TYPE::MEMBER>)

#define PHYSICS_REF_FIELD(TYPE, MEMBER, TYPE_NAME, REF_TYPE) \
    Reflection::FieldInfo(#MEMBER, TYPE_NAME, Reflection::FieldKind::ObjectRef, true, GetField<TYPE, &TYPE::MEMBER>, SetField<TYPE, &TYPE::MEMBER>, REF_TYPE)
}

namespace PhysicsReflection
{
    //注册原生物理组件的持久化字段
    void Register()
    {
        static bool registered = false;
        if (registered) return;
        registered = true;

        Reflection::RegisterTypeFields(RigidBodyComponent::StaticType(),
        {
            PHYSICS_FIELD(RigidBodyComponent, enabled, "bool", Bool),
            PHYSICS_FIELD(RigidBodyComponent, bodyType, "PhysicsBodyType", UInt32),
            PHYSICS_FIELD(RigidBodyComponent, mass, "float32", Float32),
            PHYSICS_FIELD(RigidBodyComponent, useGravity, "bool", Bool),
            PHYSICS_FIELD(RigidBodyComponent, linearDamping, "float32", Float32),
            PHYSICS_FIELD(RigidBodyComponent, angularDamping, "float32", Float32),
            PHYSICS_FIELD(RigidBodyComponent, linearVelocity, "vector3", Vector3),
            PHYSICS_FIELD(RigidBodyComponent, angularVelocity, "vector3", Vector3),
            PHYSICS_FIELD(RigidBodyComponent, continuousCollisionDetection, "bool", Bool),
            PHYSICS_FIELD(RigidBodyComponent, lockFlags, "uint32", UInt32),
        });

        Reflection::RegisterTypeFields(ColliderComponent::StaticType(),
        {
            PHYSICS_FIELD(ColliderComponent, enabled, "bool", Bool),
            PHYSICS_FIELD(ColliderComponent, shape, "ColliderShape", UInt32),
            PHYSICS_FIELD(ColliderComponent, isTrigger, "bool", Bool),
            PHYSICS_FIELD(ColliderComponent, center, "vector3", Vector3),
            PHYSICS_FIELD(ColliderComponent, halfExtents, "vector3", Vector3),
            PHYSICS_FIELD(ColliderComponent, radius, "float32", Float32),
            PHYSICS_FIELD(ColliderComponent, halfHeight, "float32", Float32),
            PHYSICS_REF_FIELD(ColliderComponent, mesh, "Ref<Mesh>", "Mesh"),
            PHYSICS_FIELD(ColliderComponent, staticFriction, "float32", Float32),
            PHYSICS_FIELD(ColliderComponent, dynamicFriction, "float32", Float32),
            PHYSICS_FIELD(ColliderComponent, restitution, "float32", Float32),
            PHYSICS_FIELD(ColliderComponent, collisionLayer, "uint32", UInt32),
            PHYSICS_FIELD(ColliderComponent, collisionMask, "uint32", UInt32),
        });

        Reflection::RegisterTypeFields(CharacterControllerComponent::StaticType(),
        {
            PHYSICS_FIELD(CharacterControllerComponent, enabled, "bool", Bool),
            PHYSICS_FIELD(CharacterControllerComponent, shape, "CharacterControllerShape", UInt32),
            PHYSICS_FIELD(CharacterControllerComponent, radius, "float32", Float32),
            PHYSICS_FIELD(CharacterControllerComponent, height, "float32", Float32),
            PHYSICS_FIELD(CharacterControllerComponent, halfExtents, "vector3", Vector3),
            PHYSICS_FIELD(CharacterControllerComponent, stepOffset, "float32", Float32),
            PHYSICS_FIELD(CharacterControllerComponent, contactOffset, "float32", Float32),
            PHYSICS_FIELD(CharacterControllerComponent, slopeLimit, "float32", Float32),
            PHYSICS_FIELD(CharacterControllerComponent, minMoveDistance, "float32", Float32),
            PHYSICS_FIELD(CharacterControllerComponent, collisionLayer, "uint32", UInt32),
            PHYSICS_FIELD(CharacterControllerComponent, collisionMask, "uint32", UInt32),
        });
    }
}

#undef PHYSICS_FIELD
#undef PHYSICS_REF_FIELD
