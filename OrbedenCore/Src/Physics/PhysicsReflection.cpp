#include "Physics/PhysicsReflection.h"

#include "Runtime/Object/CharacterController.h"
#include "Runtime/Object/Collider.h"
#include "Runtime/Object/RigidBody.h"
#include "Runtime/Object/HeightField.h"
#include "Runtime/Object/WheelCollider.h"
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

    //地形字段修改后重建高度和网格，使序列化与 Inspector 使用同一更新路径。
    template<auto Member>
    bool SetHeightField(Object* object, const std::string& value)
    {
        HeightField* terrain = static_cast<HeightField*>(object);
        if (!Reflection::SetFromXmlValue(terrain->*Member, value)) return false;
        terrain->Regenerate();
        return true;
    }

#define HEIGHT_FIELD(MEMBER, TYPE_NAME, KIND) \
    Reflection::FieldInfo(#MEMBER, TYPE_NAME, Reflection::FieldKind::KIND, true, GetField<HeightField, &HeightField::MEMBER>, SetHeightField<&HeightField::MEMBER>)

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

        Reflection::RegisterTypeFields(HeightField::StaticType(),
        {
            PHYSICS_FIELD(HeightField, enabled, "bool", Bool),
            HEIGHT_FIELD(seed, "int32", Int32),
            HEIGHT_FIELD(sampleTileX, "int32", Int32),
            HEIGHT_FIELD(sampleTileZ, "int32", Int32),
            HEIGHT_FIELD(sizeX, "float32", Float32),
            HEIGHT_FIELD(sizeZ, "float32", Float32),
            HEIGHT_FIELD(rowCount, "int32", Int32),
            HEIGHT_FIELD(columnCount, "int32", Int32),
            HEIGHT_FIELD(amplitude, "float32", Float32),
            HEIGHT_FIELD(frequency, "float32", Float32),
            HEIGHT_FIELD(octaves, "int32", Int32),
            HEIGHT_FIELD(flattenMinX, "float32", Float32),
            HEIGHT_FIELD(flattenMaxX, "float32", Float32),
            HEIGHT_FIELD(flattenMinZ, "float32", Float32),
            HEIGHT_FIELD(flattenMaxZ, "float32", Float32),
            HEIGHT_FIELD(flattenHeight, "float32", Float32),
            HEIGHT_FIELD(flattenBlendDistance, "float32", Float32),
            HEIGHT_FIELD(collisionLayer, "uint32", UInt32),
            HEIGHT_FIELD(generateNoiseTexture, "bool", Bool),
            HEIGHT_FIELD(noiseTextureSize, "int32", Int32),
            HEIGHT_FIELD(noiseLowColor, "color", Color),
            HEIGHT_FIELD(noiseHighColor, "color", Color),
            HEIGHT_FIELD(tileSize, "float32", Float32),
            Reflection::FieldInfo("material", "Ref<Material>", Reflection::FieldKind::ObjectRef, true,
                GetField<HeightField, &HeightField::material>,
                SetHeightField<&HeightField::material>, "Material"),
        });

        Reflection::RegisterTypeFields(WheelCollider::StaticType(),
        {
            PHYSICS_FIELD(WheelCollider, enabled, "bool", Bool),
            PHYSICS_FIELD(WheelCollider, wheelOffset, "vector3", Vector3),
            PHYSICS_FIELD(WheelCollider, wheelRadius, "float32", Float32),
            PHYSICS_FIELD(WheelCollider, suspensionRestLength, "float32", Float32),
            PHYSICS_FIELD(WheelCollider, suspensionTravel, "float32", Float32),
            PHYSICS_FIELD(WheelCollider, suspensionStiffness, "float32", Float32),
            PHYSICS_FIELD(WheelCollider, suspensionDamping, "float32", Float32),
            PHYSICS_FIELD(WheelCollider, rollingFriction, "float32", Float32),
            PHYSICS_FIELD(WheelCollider, lateralFriction, "float32", Float32),
            PHYSICS_FIELD(WheelCollider, raycastDistance, "float32", Float32),
            PHYSICS_FIELD(WheelCollider, groundQueryLayer, "uint32", UInt32),
            PHYSICS_FIELD(WheelCollider, steeringWheel, "bool", Bool),
            PHYSICS_FIELD(WheelCollider, steerAngle, "float32", Float32),
        });

        Reflection::RegisterTypeFields(RigidBody::StaticType(),
        {
            PHYSICS_FIELD(RigidBody, enabled, "bool", Bool),
            PHYSICS_FIELD(RigidBody, bodyType, "PhysicsBodyType", UInt32),
            PHYSICS_FIELD(RigidBody, mass, "float32", Float32),
            PHYSICS_FIELD(RigidBody, useGravity, "bool", Bool),
            PHYSICS_FIELD(RigidBody, linearDamping, "float32", Float32),
            PHYSICS_FIELD(RigidBody, angularDamping, "float32", Float32),
            PHYSICS_FIELD(RigidBody, linearVelocity, "vector3", Vector3),
            PHYSICS_FIELD(RigidBody, angularVelocity, "vector3", Vector3),
            PHYSICS_FIELD(RigidBody, continuousCollisionDetection, "bool", Bool),
            PHYSICS_FIELD(RigidBody, lockFlags, "uint32", UInt32),
        });

        Reflection::RegisterTypeFields(BoxCollider::StaticType(),
        {
            COLLIDER_COMMON_FIELDS(BoxCollider),
            PHYSICS_FIELD(BoxCollider, halfExtents, "vector3", Vector3),
        });

        Reflection::RegisterTypeFields(SphereCollider::StaticType(),
        {
            COLLIDER_COMMON_FIELDS(SphereCollider),
            PHYSICS_FIELD(SphereCollider, radius, "float32", Float32),
        });

        Reflection::RegisterTypeFields(CapsuleCollider::StaticType(),
        {
            COLLIDER_COMMON_FIELDS(CapsuleCollider),
            PHYSICS_FIELD(CapsuleCollider, radius, "float32", Float32),
            PHYSICS_FIELD(CapsuleCollider, halfHeight, "float32", Float32),
        });

        Reflection::RegisterTypeFields(ConvexMeshCollider::StaticType(),
        {
            COLLIDER_COMMON_FIELDS(ConvexMeshCollider),
            PHYSICS_REF_FIELD(ConvexMeshCollider, mesh, "Ref<Mesh>", "Mesh"),
        });

        Reflection::RegisterTypeFields(TriangleMeshCollider::StaticType(),
        {
            COLLIDER_COMMON_FIELDS(TriangleMeshCollider),
            PHYSICS_REF_FIELD(TriangleMeshCollider, mesh, "Ref<Mesh>", "Mesh"),
        });

        Reflection::RegisterTypeFields(CharacterController::StaticType(),
        {
            PHYSICS_FIELD(CharacterController, enabled, "bool", Bool),
            PHYSICS_FIELD(CharacterController, shape, "CharacterControllerShape", UInt32),
            PHYSICS_FIELD(CharacterController, radius, "float32", Float32),
            PHYSICS_FIELD(CharacterController, height, "float32", Float32),
            PHYSICS_FIELD(CharacterController, halfExtents, "vector3", Vector3),
            PHYSICS_FIELD(CharacterController, stepOffset, "float32", Float32),
            PHYSICS_FIELD(CharacterController, contactOffset, "float32", Float32),
            PHYSICS_FIELD(CharacterController, slopeLimit, "float32", Float32),
            PHYSICS_FIELD(CharacterController, minMoveDistance, "float32", Float32),
            PHYSICS_FIELD(CharacterController, collisionLayer, "uint32", UInt32),
            PHYSICS_FIELD(CharacterController, collisionMask, "uint32", UInt32),
        });
    }
}

#undef PHYSICS_FIELD
#undef PHYSICS_REF_FIELD
#undef COLLIDER_COMMON_FIELDS

#undef HEIGHT_FIELD
