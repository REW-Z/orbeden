#pragma once
#include "Rendering/RenderTypes.h"

#include <span>

//级联阴影数学参数
struct ShadowCascadeSettings
{
    int32 count = 0;
    int32 resolution = 2048;
    float32 blendRatio = 0.1f;
    float32 depthBias = 0.0005f;
    float32 normalBias = 0.25f;
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

//裁剪手动分区并计算稳定级联投影
class ShadowCascadeBuilder
{
public:
    //校验用户配置
    static ShadowCascadeSettings ValidateSettings(const ShadowCascadeSettings& settings);
    //裁剪到相机范围并返回有效级联数量
    static int32 BuildSplits(std::span<const float32> cascadeEnds, float32 nearPlane, float32 farPlane,
        std::span<float32> splits);
    //建立球包围、纹素对齐与上游投射物深度范围
    static ShadowCascade BuildCascade(const matrix4x4& cameraWorld, const matrix4x4& projection,
        const vector3& lightDirection, float32 sliceNear, float32 sliceFar,
        const ShadowCascadeSettings& settings, const List<bounds3>& casterBounds);
};
