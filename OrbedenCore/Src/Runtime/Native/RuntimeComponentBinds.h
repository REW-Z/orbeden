#pragma once

#include "Defines/types.h"
#include "Runtime/EnsId.h"

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
    void* GetName = nullptr;
    void* SetName = nullptr;
    void* HasSpaceComponent = nullptr;
    void* HasStaticMeshRenderer = nullptr;
    void* AddStaticMeshRenderer = nullptr;
    void* GetSpaceComponent = nullptr;
    void* GetStaticMeshRenderer = nullptr;

    //创建 Ens 函数表。
    static EnsBind Create();
};

//SpaceComponent 原生函数表。
struct SpaceComponentBind
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

    //创建 SpaceComponent 函数表。
    static SpaceComponentBind Create();
};

//StaticMeshRenderer 原生函数表。
struct StaticMeshRendererBind
{
public:
    void* GetEnabled = nullptr;
    void* SetEnabled = nullptr;
    void* GetMesh = nullptr;
    void* SetMesh = nullptr;
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
    void* HasComponent = nullptr;
    void* AddComponent = nullptr;
    void* GetComponent = nullptr;
    void* GetEnabled = nullptr;
    void* SetEnabled = nullptr;
    void* GetShape = nullptr;
    void* SetShape = nullptr;
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
