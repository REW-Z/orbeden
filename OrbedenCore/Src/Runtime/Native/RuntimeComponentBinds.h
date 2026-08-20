#pragma once

#include "Defines/types.h"
#include "Runtime/EnsId.h"
#include "Runtime/Native/NativeApiAbi.h"

#pragma pack(push, 8)

//World 原生函数表。
struct WorldBind
{
public:
    void* CreateEns = nullptr;
    void* CreateEnsWithStableId = nullptr;
    void* FindEns = nullptr;
    void* DestroyEns = nullptr;

    //创建 World 函数表。
    static WorldBind Create();
};

//PathDefines 原生函数表。
struct PathDefinesBind
{
public:
    void* GetContentRoot = nullptr;
    void* GetContentFilePath = nullptr;

    //创建 PathDefines 函数表。
    static PathDefinesBind Create();
};

//Ens 原生函数表。
struct EnsBind
{
public:
    void* IsAlive = nullptr;
    void* GetLocalActive = nullptr;
    void* GetWorldActive = nullptr;
    void* SetLocalActive = nullptr;
    void* GetName = nullptr;
    void* SetName = nullptr;
    void* HasTransformComponent = nullptr;
    void* HasStaticMeshRenderer = nullptr;
    void* AddStaticMeshRenderer = nullptr;
    void* GetTransformComponent = nullptr;
    void* GetStaticMeshRenderer = nullptr;

    //创建 Ens 函数表。
    static EnsBind Create();
};

//TransformComponent 原生函数表。
struct TransformComponentBind
{
public:
    void* GetParent = nullptr;
    void* SetParent = nullptr;
    void* GetLocalPosition = nullptr;
    void* SetLocalPosition = nullptr;
    void* GetLocalRotation = nullptr;
    void* SetLocalRotation = nullptr;
    void* GetLocalScale = nullptr;
    void* SetLocalScale = nullptr;
    void* GetWorldPosition = nullptr;
    void* GetWorldRotation = nullptr;

    //创建 TransformComponent 函数表。
    static TransformComponentBind Create();
};

//StaticMeshRenderer 原生函数表。
struct StaticMeshRendererBind
{
public:
    void* GetEnabled = nullptr;
    void* SetEnabled = nullptr;
    void* GetMesh = nullptr;
    void* SetMesh = nullptr;
    void* GetDrawQueue = nullptr;
    void* SetDrawQueue = nullptr;
    void* GetCastShadows = nullptr;
    void* SetCastShadows = nullptr;
    void* GetReceiveShadows = nullptr;
    void* SetReceiveShadows = nullptr;

    //创建 StaticMeshRenderer 函数表。
    static StaticMeshRendererBind Create();
};

//RigidBodyComponent 原生函数表。
struct RigidBodyBind
{
public:
    void* HasComponent = nullptr;
    void* AddComponent = nullptr;
    void* GetComponent = nullptr;
    void* GetEnabled = nullptr;
    void* SetEnabled = nullptr;
    void* GetBodyType = nullptr;
    void* SetBodyType = nullptr;
    void* GetMass = nullptr;
    void* SetMass = nullptr;
    void* GetUseGravity = nullptr;
    void* SetUseGravity = nullptr;
    void* GetLinearDamping = nullptr;
    void* SetLinearDamping = nullptr;
    void* GetAngularDamping = nullptr;
    void* SetAngularDamping = nullptr;
    void* GetLinearVelocity = nullptr;
    void* SetLinearVelocity = nullptr;
    void* GetAngularVelocity = nullptr;
    void* SetAngularVelocity = nullptr;
    void* GetContinuousCollisionDetection = nullptr;
    void* SetContinuousCollisionDetection = nullptr;
    void* GetLockFlags = nullptr;
    void* SetLockFlags = nullptr;

    //创建 RigidBodyComponent 函数表。
    static RigidBodyBind Create();
};

//ColliderComponent 原生函数表。
struct ColliderBind
{
public:
    void* AddBoxCollider = nullptr;
    void* AddSphereCollider = nullptr;
    void* AddCapsuleCollider = nullptr;
    void* AddConvexMeshCollider = nullptr;
    void* AddTriangleMeshCollider = nullptr;
    void* GetColliderCount = nullptr;
    void* GetColliderAt = nullptr;
    void* GetGeometryType = nullptr;
    void* GetEnabled = nullptr;
    void* SetEnabled = nullptr;
    void* GetIsTrigger = nullptr;
    void* SetIsTrigger = nullptr;
    void* GetCenter = nullptr;
    void* SetCenter = nullptr;
    void* GetHalfExtents = nullptr;
    void* SetHalfExtents = nullptr;
    void* GetRadius = nullptr;
    void* SetRadius = nullptr;
    void* GetHalfHeight = nullptr;
    void* SetHalfHeight = nullptr;
    void* GetMesh = nullptr;
    void* SetMesh = nullptr;
    void* GetStaticFriction = nullptr;
    void* SetStaticFriction = nullptr;
    void* GetDynamicFriction = nullptr;
    void* SetDynamicFriction = nullptr;
    void* GetRestitution = nullptr;
    void* SetRestitution = nullptr;
    void* GetCollisionLayer = nullptr;
    void* SetCollisionLayer = nullptr;
    void* GetCollisionMask = nullptr;
    void* SetCollisionMask = nullptr;

    //创建 ColliderComponent 函数表。
    static ColliderBind Create();
};

//CharacterControllerComponent 原生函数表。
struct CharacterControllerBind
{
public:
    void* HasComponent = nullptr;
    void* AddComponent = nullptr;
    void* GetComponent = nullptr;
    void* GetEnabled = nullptr;
    void* SetEnabled = nullptr;
    void* GetShape = nullptr;
    void* SetShape = nullptr;
    void* GetRadius = nullptr;
    void* SetRadius = nullptr;
    void* GetHeight = nullptr;
    void* SetHeight = nullptr;
    void* GetHalfExtents = nullptr;
    void* SetHalfExtents = nullptr;
    void* GetStepOffset = nullptr;
    void* SetStepOffset = nullptr;
    void* GetContactOffset = nullptr;
    void* SetContactOffset = nullptr;
    void* GetSlopeLimit = nullptr;
    void* SetSlopeLimit = nullptr;
    void* GetMinMoveDistance = nullptr;
    void* SetMinMoveDistance = nullptr;
    void* GetCollisionLayer = nullptr;
    void* SetCollisionLayer = nullptr;
    void* GetCollisionMask = nullptr;
    void* SetCollisionMask = nullptr;

    //创建 CharacterControllerComponent 函数表。
    static CharacterControllerBind Create();
};

#pragma pack(pop)

ORBEDEN_ASSERT_NATIVE_API_TABLE(WorldBind, 4);
ORBEDEN_ASSERT_NATIVE_API_TABLE(PathDefinesBind, 2);
ORBEDEN_ASSERT_NATIVE_API_TABLE(EnsBind, 11);
ORBEDEN_ASSERT_NATIVE_API_TABLE(TransformComponentBind, 10);
ORBEDEN_ASSERT_NATIVE_API_TABLE(StaticMeshRendererBind, 10);
ORBEDEN_ASSERT_NATIVE_API_TABLE(RigidBodyBind, 23);
ORBEDEN_ASSERT_NATIVE_API_TABLE(ColliderBind, 32);
ORBEDEN_ASSERT_NATIVE_API_TABLE(CharacterControllerBind, 25);

ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, AddBoxCollider, 0);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, AddSphereCollider, 1);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, AddCapsuleCollider, 2);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, AddConvexMeshCollider, 3);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, AddTriangleMeshCollider, 4);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetColliderCount, 5);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetColliderAt, 6);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetGeometryType, 7);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetEnabled, 8);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetEnabled, 9);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetIsTrigger, 10);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetIsTrigger, 11);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetCenter, 12);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetCenter, 13);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetHalfExtents, 14);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetHalfExtents, 15);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetRadius, 16);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetRadius, 17);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetHalfHeight, 18);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetHalfHeight, 19);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetMesh, 20);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetMesh, 21);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetStaticFriction, 22);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetStaticFriction, 23);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetDynamicFriction, 24);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetDynamicFriction, 25);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetRestitution, 26);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetRestitution, 27);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetCollisionLayer, 28);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetCollisionLayer, 29);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, GetCollisionMask, 30);
ORBEDEN_ASSERT_NATIVE_API_SLOT(ColliderBind, SetCollisionMask, 31);
