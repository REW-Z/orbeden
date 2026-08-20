#pragma once

#include "Runtime/EngineTypes.h"
#include "Runtime/EnsId.h"

//刚体运动类型
enum class PhysicsBodyType : uint32
{
    Static = 0,
    Dynamic = 1,
    Kinematic = 2,
};

//角色控制器几何类型
enum class CharacterControllerShape : uint32
{
    Capsule = 0,
    Box = 1,
};

//刚体轴锁定位
enum PhysicsLockFlag : uint32
{
    PhysicsLockNone = 0,
    PhysicsLockPositionX = 1u << 0,
    PhysicsLockPositionY = 1u << 1,
    PhysicsLockPositionZ = 1u << 2,
    PhysicsLockRotationX = 1u << 3,
    PhysicsLockRotationY = 1u << 4,
    PhysicsLockRotationZ = 1u << 5,
};

//物理事件类型
enum class PhysicsEventType : uint32
{
    ContactEnter = 0,
    ContactStay = 1,
    ContactExit = 2,
    TriggerEnter = 3,
    TriggerExit = 4,
};

//场景查询命中信息
struct PhysicsQueryHit
{
public:
    EnsId ens;
    vector3 position;
    vector3 normal;
    float32 distance = 0.0f;
};

//碰撞和触发器事件
struct PhysicsEvent
{
public:
    PhysicsEventType type = PhysicsEventType::ContactEnter;
    EnsId first;
    EnsId second;
    vector3 position;
    vector3 normal;
    float32 impulse = 0.0f;
};

//角色移动结果位
enum CharacterCollisionFlag : uint32
{
    CharacterCollisionNone = 0,
    CharacterCollisionSides = 1u << 0,
    CharacterCollisionUp = 1u << 1,
    CharacterCollisionDown = 1u << 2,
};
