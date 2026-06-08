#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EnsId.h"

//相机组件，保存渲染视角的公开参数
class Camera : public Component
{
    OBJECT_TYPE_DECLARE(Camera)

public:
    bool enabled = true;
    float32 fieldOfView = 60.0f;
    float32 nearPlane = 0.1f;
    float32 farPlane = 1000.0f;
    float32 depth = 0.0f;
    uint32 drawLayerMask = 0xFFFFFFFFu;
    ClearMode clearMode = ClearMode::SolidColor;
    color4 clearColor = { 0.1f, 0.12f, 0.16f, 1.0f };
};

