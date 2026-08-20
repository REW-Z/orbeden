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

#define COLLIDER_COMMON_FIELDS(TYPE) \
    PHYSICS_FIELD(TYPE, enabled, "bool", Bool), \
    PHYSICS_FIELD(TYPE, isTrigger, "bool", Bool), \
    PHYSICS_FIELD(TYPE, center, "vector3", Vector3), \
    PHYSICS_FIELD(TYPE, staticFriction, "float32", Float32), \
    PHYSICS_FIELD(TYPE, dynamicFriction, "float32", Float32), \
    PHYSICS_FIELD(TYPE, restitution, "float32", Float32), \
    PHYSICS_FIELD(TYPE, collisionLayer, "uint32", UInt32), \
    PHYSICS_FIELD(TYPE, collisionMask, "uint32", UInt32)
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

        Reflection::RegisterTypeFields(BoxColliderComponent::StaticType(),
        {
            COLLIDER_COMMON_FIELDS(BoxColliderComponent),
            PHYSICS_FIELD(BoxColliderComponent, halfExtents, "vector3", Vector3),
        });

        Reflection::RegisterTypeFields(SphereColliderComponent::StaticType(),
        {
            COLLIDER_COMMON_FIELDS(SphereColliderComponent),
            PHYSICS_FIELD(SphereColliderComponent, radius, "float32", Float32),
        });

        Reflection::RegisterTypeFields(CapsuleColliderComponent::StaticType(),
        {
            COLLIDER_COMMON_FIELDS(CapsuleColliderComponent),
            PHYSICS_FIELD(CapsuleColliderComponent, radius, "float32", Float32),
            PHYSICS_FIELD(CapsuleColliderComponent, halfHeight, "float32", Float32),
        });

        Reflection::RegisterTypeFields(ConvexMeshColliderComponent::StaticType(),
        {
            COLLIDER_COMMON_FIELDS(ConvexMeshColliderComponent),
            PHYSICS_REF_FIELD(ConvexMeshColliderComponent, mesh, "Ref<Mesh>", "Mesh"),
        });

        Reflection::RegisterTypeFields(TriangleMeshColliderComponent::StaticType(),
        {
            COLLIDER_COMMON_FIELDS(TriangleMeshColliderComponent),
            PHYSICS_REF_FIELD(TriangleMeshColliderComponent, mesh, "Ref<Mesh>", "Mesh"),
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
#undef COLLIDER_COMMON_FIELDS
