#include "Runtime/Object/WheelCollider.h"

OBJECT_TYPE_IMPLEMENT(WheelCollider, Component)

//判断最近一次模拟中机轮是否接地
bool WheelCollider::IsGrounded() const
{
    return grounded;
}

//获取最近一次模拟的悬挂压缩量
float32 WheelCollider::GetCompression() const
{
    return compression;
}
