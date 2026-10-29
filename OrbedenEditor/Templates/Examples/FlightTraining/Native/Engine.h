#pragma once

#include "Runtime/Object/Script.h"
#include "Runtime/EngineTypes.h"

//飞机发动机：处理油门输入、计算推力并施加到刚体。
class Engine final : public Script
{
    OBJECT_TYPE_DECLARE(Engine)

public:
    float32 throttleRate = 1.0f;
    float32 maxThrust = 15000.0f;
    float32 thrustFadeSpeed = 90.0f;
    //风车状态螺旋桨的等效阻力面积，平方米
    float32 windmillDragArea = 0.6f;

    float32 GetThrottle();
    float32 GetThrustNewtons();
    vector3 GetThrustForce();
    float32 GetWindmillDragNewtons();
    vector3 GetWindmillDragForce();
    void ResetThrottle();

protected:
    void OnFixedUpdate(float32 deltaTime);

private:
    float32 throttle = 0.0f;
    vector3 thrustForce;
    vector3 windmillDragForce;
};
