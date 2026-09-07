#pragma once

#include "Runtime/EngineTypes.h"
#include "Runtime/EnsId.h"

class PhysicsSystem;

//射线悬挂机轮组件：由 PhysicsSystem 在每次物理模拟后驱动。
//通过向下射线检测地面，施加弹簧阻尼悬挂力、地面摩擦与转向侧向力，
//力作用到同 Ens 的 RigidBodyComponent 上（下一物理步生效）。
//需要挂载在根节点 Dynamic 刚体所在的 Ens 上。
class WheelColliderComponent final : public Component
{
    OBJECT_TYPE_DECLARE(WheelColliderComponent)

public:
    bool enabled = true;

    //悬挂安装点相对 Ens 原点的局部偏移。
    vector3 wheelOffset = { 0.0f, -0.5f, 0.0f };

    //机轮半径，参与接地几何计算。
    float32 wheelRadius = 0.3f;

    //悬挂静止长度（安装点到地面的距离，含轮半径）。
    float32 suspensionRestLength = 0.55f;

    //悬挂最大压缩行程。
    float32 suspensionTravel = 0.35f;

    //弹簧刚度与阻尼。
    float32 suspensionStiffness = 30000.0f;
    float32 suspensionDamping = 5500.0f;

    //纵向滚动摩擦与横向防滑系数。
    float32 rollingFriction = 0.02f;
    float32 lateralFriction = 0.8f;

    //向下探测距离。
    float32 raycastDistance = 1.2f;

    //只探测指定碰撞层的地面。
    uint32 groundQueryLayer = 1u;

    //是否参与地面转向（前轮为 true）。
    bool steeringWheel = false;

    //脚本设置的目标转向角（度），只在接地时生效。
    float32 steerAngle = 0.0f;

    //判断最近一次模拟中机轮是否接地。
    bool IsGrounded() const;

    //获取最近一次模拟的悬挂压缩量。
    float32 GetCompression() const;

private:
    friend class PhysicsSystem;

    bool grounded = false;
    float32 compression = 0.0f;
    float32 previousCompression = 0.0f;
};
