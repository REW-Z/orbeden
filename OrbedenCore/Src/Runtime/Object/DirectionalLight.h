#pragma once

#include "Runtime/EnsId.h"
#include "Runtime/EngineTypes.h"

//方向光组件，描述全局平行光和基础阴影参数。
class DirectionalLight : public Component
{
    OBJECT_TYPE_DECLARE(DirectionalLight)

public:
    bool enabled = true;
    vector3 direction = { -0.35f, -1.0f, -0.45f };
    vector3 color = { 1.0f, 0.96f, 0.86f };
    float32 intensity = 1.2f;
    bool castShadows = true;
    float32 shadowBias = 0.004f;
    float32 shadowStrength = 0.45f;
    float32 shadowDistance = 24.0f;
};

