#include "Physics/PhysicsReflection.h"

#include "Physics/CharacterControllerComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/HeightFieldComponent.h"
#include "Physics/WheelColliderComponent.h"
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
        HeightFieldComponent* terrain = static_cast<HeightFieldComponent*>(object);
        if (!Reflection::SetFromXmlValue(terrain->*Member, value)) return false;
        terrain->Regenerate();
        return true;
    }

#define HEIGHT_FIELD(MEMBER, TYPE_NAME, KIND) \
    Reflection::FieldInfo(#MEMBER, TYPE_NAME, Reflection::FieldKind::KIND, true, GetField<HeightFieldComponent, &HeightFieldComponent::MEMBER>, SetHeightField<&HeightFieldComponent::MEMBER>)

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

        Reflection::RegisterTypeFields(HeightFieldComponent::StaticType(),
        {
            PHYSICS_FIELD(HeightFieldComponent, enabled, "bool", Bool),
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
                GetField<HeightFieldComponent, &HeightFieldComponent::material>,
                SetHeightField<&HeightFieldComponent::material>, "Material"),
        });

        Reflection::RegisterTypeFields(WheelColliderComponent::StaticType(),
        {
            PHYSICS_FIELD(WheelColliderComponent, enabled, "bool", Bool),
            PHYSICS_FIELD(WheelColliderComponent, wheelOffset, "vector3", Vector3),
            PHYSICS_FIELD(WheelColliderComponent, wheelRadius, "float32", Float32),
            PHYSICS_FIELD(WheelColliderComponent, suspensionRestLength, "float32", Float32),
            PHYSICS_FIELD(WheelColliderComponent, suspensionTravel, "float32", Float32),
            PHYSICS_FIELD(WheelColliderComponent, suspensionStiffness, "float32", Float32),
            PHYSICS_FIELD(WheelColliderComponent, suspensionDamping, "float32", Float32),
            PHYSICS_FIELD(WheelColliderComponent, rollingFriction, "float32", Float32),
            PHYSICS_FIELD(WheelColliderComponent, lateralFriction, "float32", Float32),
            PHYSICS_FIELD(WheelColliderComponent, raycastDistance, "float32", Float32),
            PHYSICS_FIELD(WheelColliderComponent, groundQueryLayer, "uint32", UInt32),
            PHYSICS_FIELD(WheelColliderComponent, steeringWheel, "bool", Bool),
            PHYSICS_FIELD(WheelColliderComponent, steerAngle, "float32", Float32),
        });

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

#undef HEIGHT_FIELD
