#pragma once

#include "Scripting/ScriptBehaviour.h"
#include "Runtime/EnsId.h"
#include "Runtime/EngineTypes.h"

//保持地平线稳定的自由环绕相机，目标为场景中的父级飞机。
class FlightOrbitCamera final : public ScriptBehaviour
{
    OBJECT_TYPE_DECLARE(FlightOrbitCamera)

public:
    float32 distance = 10.0f;
    float32 sensitivity = 0.25f;
    float32 defaultElevation = 18.0f;

private:
    EnsId target;
    vector3 initialPosition;
    quaternion initialRotation;
    float32 azimuth = 0;
    float32 elevation = 18;
    vector2 lastMousePosition;
    bool dragging = false;

protected:
    /// <summary>记录目标并脱离飞机父级，避免跟随机身翻滚。</summary>
    void OnStart();

    /// <summary>读取拖动输入并在物理更新后围绕飞机定位。</summary>
    void OnLateUpdate(float32 deltaTime);

    /// <summary>停止时恢复相机层级和初始机尾视角。</summary>
    void OnEnd();
};
