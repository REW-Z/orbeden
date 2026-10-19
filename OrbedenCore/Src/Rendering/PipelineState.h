#pragma once

#include "Rendering/RenderTypes.h"

//图元拓扑
enum class PrimitiveTopology : uint8
{
    Triangles,
    Lines,
};

//多边形填充方式
enum class PolygonMode : uint8
{
    Fill,
    Line,
};

//一次绘制的固定功能状态
struct PipelineState
{
    bool depthTest = true;
    bool depthWrite = true;
    DepthCompare depthCompare = DepthCompare::Less;
    bool blend = false;
    BlendMode blendMode = BlendMode::Alpha;
    CullMode cull = CullMode::None;
    PolygonMode polygonMode = PolygonMode::Fill;
    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    bool depthBias = false;
    float32 depthBiasSlope = 0.0f;
    float32 depthBiasConstant = 0.0f;
};
