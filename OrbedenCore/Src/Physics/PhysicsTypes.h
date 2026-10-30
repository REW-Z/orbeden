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
    //实际命中的碰撞体节点，复合刚体可返回子节点
    EnsId ens;
    vector3 position;
    vector3 normal;
    float32 distance = 0.0f;
};

//场景查询的附加过滤条件。粒子用它在同一次查询里同时排除自己与触发器。
struct PhysicsQueryFilter
{
public:
    uint32 layerMask = 0xFFFFFFFFu;
    //排除此碰撞体节点；传入刚体节点时排除整个复合刚体
    EnsId ignoredEns;
    //默认排除触发器，粒子只与实体几何碰撞
    bool includeTriggers = false;
};

//碰撞和触发器事件
struct PhysicsEvent
{
public:
    PhysicsEventType type = PhysicsEventType::ContactEnter;
    //双方所属刚体节点，独立静态碰撞体使用自身节点
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
