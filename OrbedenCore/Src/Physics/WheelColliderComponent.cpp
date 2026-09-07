#include "Physics/WheelColliderComponent.h"

OBJECT_TYPE_IMPLEMENT(WheelColliderComponent, Component)

//判断最近一次模拟中机轮是否接地
bool WheelColliderComponent::IsGrounded() const
{
    return grounded;
}

//获取最近一次模拟的悬挂压缩量
float32 WheelColliderComponent::GetCompression() const
{
    return compression;
}
