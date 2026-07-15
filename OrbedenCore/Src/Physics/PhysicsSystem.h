#pragma once

#include "Application.h"
#include "Physics/PhysicsTypes.h"

namespace physx
{
    class PxController;
    class PxControllerManager;
    class PxFoundation;
    class PxPhysics;
    class PxRigidActor;
    class PxScene;
    struct PxCookingParams;
}

//PhysX 5 CPU-only 原生物理系统
class PhysicsSystem final : public IEngineSystem
{
private:
    class Impl;
    Impl* impl = nullptr;

public:
    PhysicsSystem();
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;
    ~PhysicsSystem() override;

    //创建 PhysX Foundation、Scene、Cooking 和 CCT 状态
    bool Initialize();

    //释放全部原生物理状态
    void Shutdown();

    //判断物理系统是否可用
    bool IsInitialized() const;

    //清空当前 World 对应的原生 Actor、CCT 和事件
    void ResetWorld();

    //执行固定步长同步和模拟
    void FixedUpdate(World& world, float fixedDeltaTime) override;

    //发射射线并返回最近命中
    bool Raycast(const vector3& origin, const vector3& direction, float32 distance, PhysicsQueryHit& hit, uint32 layerMask = 0xFFFFFFFFu) const;

    //使用球体扫描并返回最近命中
    bool SweepSphere(const vector3& origin, float32 radius, const vector3& direction, float32 distance, PhysicsQueryHit& hit, uint32 layerMask = 0xFFFFFFFFu) const;

    //收集与球体重叠的实体
    uint32 OverlapSphere(const vector3& center, float32 radius, List<PhysicsQueryHit>& hits, uint32 layerMask = 0xFFFFFFFFu) const;

    //移动角色控制器并返回 CharacterCollisionFlag 位
    uint32 MoveCharacter(EnsId ens, const vector3& displacement, float32 deltaTime);

    //将角色控制器脚底移动到指定世界坐标
    bool TeleportCharacter(EnsId ens, const vector3& footPosition);

    //读取最近一次固定步产生的物理事件
    const List<PhysicsEvent>& GetEvents() const;

    //立即清空事件队列
    void ClearEvents();

    //获取原生 Foundation，供高级 C++ 模块使用
    physx::PxFoundation* GetFoundation() const;

    //获取原生 Physics，供 joints/articulations/vehicle 等模块使用
    physx::PxPhysics* GetPhysics() const;

    //获取原生 Scene
    physx::PxScene* GetScene() const;

    //获取 PhysX 5.9 无状态 Cooking 参数
    const physx::PxCookingParams* GetCookingParams() const;

    //获取原生 CCT 管理器
    physx::PxControllerManager* GetControllerManager() const;

    //查找实体对应的原生刚体 Actor
    physx::PxRigidActor* GetRigidActor(EnsId ens) const;

    //查找实体对应的原生角色控制器
    physx::PxController* GetController(EnsId ens) const;
};
