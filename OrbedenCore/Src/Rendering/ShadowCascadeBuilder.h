#pragma once
#include "Rendering/Backend/RenderBackend.h"

//级联阴影数学参数
struct ShadowCascadeSettings
{
    static constexpr int32 MaxCascades = 6;
    int32 count = 4;
    int32 resolution = 2048;
    float32 distance = 20000.0f;
    float32 splitLambda = 1.0f;
    float32 blendRatio = 0.1f;
    float32 depthBias = 0.0005f;
    float32 normalBias = 0.25f;
    bool adaptive = true;
    int32 debugView = 0;
};

//单级投影与采样尺度
struct ShadowCascade
{
    matrix4x4 worldToShadow;
    frustum lightFrustum;
    float32 splitDepth = 0.0f;
    float32 blendStart = 0.0f;
    float32 texelWorldSize = 0.0f;
    float32 depthRange = 1.0f;
};

//计算稳定级联投影与采样分布分区
class ShadowCascadeBuilder
{
public:
    //校验用户配置
    static ShadowCascadeSettings ValidateSettings(const ShadowCascadeSettings& settings);
    //计算完整覆盖的级联边界
    static void BuildSplits(const ShadowCascadeSettings& settings, float32 nearPlane, float32 farPlane,
        const GpuDepthDistribution* distribution, const float32* previousSplits, float32 deltaTime, float32* splits);
    //建立球包围、纹素对齐与上游投射物深度范围
    static ShadowCascade BuildCascade(const matrix4x4& cameraWorld, const matrix4x4& projection,
        const vector3& lightDirection, float32 sliceNear, float32 sliceFar,
        const ShadowCascadeSettings& settings, const List<bounds3>& casterBounds);
};
