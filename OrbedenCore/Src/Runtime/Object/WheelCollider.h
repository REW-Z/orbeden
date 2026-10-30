#pragma once

#include "Runtime/EngineTypes.h"
#include "Runtime/Object/Component.h"

class PhysicsSystem;

//射线悬挂机轮组件：由 PhysicsSystem 在每次物理模拟前驱动。
//通过向下射线检测地面，施加弹簧阻尼悬挂力、地面摩擦与转向侧向力，
//力作用到自身或最近祖先的 Dynamic RigidBody 上（当前物理步生效）
//刚体保持在根节点，车轮可挂载到任意深度的子节点
//子节点只提供层级位置，机轮方向、偏移和尺寸换算使用所属刚体坐标系
class WheelCollider final : public Component
{
    OBJECT_TYPE_DECLARE(WheelCollider)

public:
    bool enabled = true;

    //相对车轮 Ens 位置的安装点偏移，方向和缩放取所属刚体
    vector3 wheelOffset = { 0.0f, -0.5f, 0.0f };

    //机轮半径，参与接地几何计算。
    float32 wheelRadius = 0.3f;

    //悬挂静止长度（安装点到轮心的距离，不含轮半径）。
    float32 suspensionRestLength = 0.55f;

    //悬挂最大压缩行程。
    float32 suspensionTravel = 0.35f;

    //弹簧刚度与阻尼。
    float32 suspensionStiffness = 30000.0f;
    float32 suspensionDamping = 5500.0f;

    //纵向滚动摩擦与横向防滑系数。
    float32 rollingFriction = 0.02f;
    float32 lateralFriction = 0.8f;

    //沿所属刚体局部 -Y 探测的距离，与悬挂长度一起按刚体 Y 轴的世界缩放换算
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
