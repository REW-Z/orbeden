#include "Rendering/ShadowCascadeBuilder.h"
#include "Rendering/RenderMath.h"
#include <algorithm>
#include <cmath>

namespace
{
    //限制有限浮点参数
    float32 ClampFinite(float32 value, float32 minimum, float32 maximum, float32 defaultValue)
    {
        return std::isfinite(value) ? std::clamp(value, minimum, maximum) : defaultValue;
    }
}

ShadowCascadeSettings ShadowCascadeBuilder::ValidateSettings(const ShadowCascadeSettings& source)
{
    ShadowCascadeSettings settings = source;
    settings.count = std::clamp(settings.count, 1, ShadowCascadeSettings::MaxCascades);
    int32 requested = std::clamp(settings.resolution, 256, 4096);
    settings.resolution = 256;
    while (settings.resolution * 2 <= requested) settings.resolution *= 2;
    settings.distance = ClampFinite(settings.distance, 0.01f, 10000000.0f, 20000.0f);
    settings.splitLambda = ClampFinite(settings.splitLambda, 0.0f, 1.0f, 1.0f);
    settings.blendRatio = ClampFinite(settings.blendRatio, 0.0f, 0.3f, 0.1f);
    settings.depthBias = ClampFinite(settings.depthBias, 0.0f, 10.0f, 0.0005f);
    settings.normalBias = ClampFinite(settings.normalBias, 0.0f, 4.0f, 0.25f);
    settings.debugView = std::clamp(settings.debugView, 0, 2);
    return settings;
}

void ShadowCascadeBuilder::BuildSplits(const ShadowCascadeSettings& source, float32 nearPlane, float32 farPlane,
    const GpuDepthDistribution* distribution, const float32* previousSplits, float32 deltaTime, float32* splits)
{
    const ShadowCascadeSettings settings = ValidateSettings(source);
    const float32 n = ClampFinite(nearPlane, 0.001f, 10000000.0f, 0.1f);
    const float32 f = std::max(n + 0.001f, std::min(settings.distance,
        ClampFinite(farPlane, n + 0.001f, 10000000.0f, 1000.0f)));
    const float32 gap = (f - n) * 0.000001f;
    uint64 total = 0;
    if (settings.adaptive && distribution)
        for (uint32 count : distribution->bins) total += count;
    const float32 weight = 1.0f - std::exp(-6.0f * ClampFinite(deltaTime, 0.0f, 0.1f, 0.0f));

    //混合固定分区与实际样本分位数
    for (int32 index = 0; index < settings.count; ++index)
    {
        float32 ratio = static_cast<float32>(index + 1) / settings.count;
        float32 baseline = (1.0f - settings.splitLambda) * (n + (f - n) * ratio)
            + settings.splitLambda * n * std::pow(f / n, ratio);
        float32 split = baseline;
        if (total && index + 1 < settings.count && std::isfinite(distribution->nearPlane) &&
            std::isfinite(distribution->farPlane) && distribution->nearPlane > 0.0f &&
            distribution->farPlane > distribution->nearPlane)
        {
            float64 target = static_cast<float64>(total) * ratio;
            uint64 cumulative = 0;
            for (int32 bin = 0; bin < GpuDepthDistribution::BinCount; ++bin)
            {
                uint32 count = distribution->bins[bin];
                if (count && static_cast<float64>(cumulative + count) >= target)
                {
                    float32 fraction = static_cast<float32>((target - cumulative) / count);
                    float32 quantile = distribution->nearPlane * std::pow(distribution->farPlane / distribution->nearPlane,
                        (bin + fraction) / GpuDepthDistribution::BinCount);
                    quantile = std::clamp(quantile, baseline * 0.5f, baseline * 2.0f);
                    split = std::exp(std::log(baseline) * 0.65f + std::log(quantile) * 0.35f);
                    break;
                }
                cumulative += count;
            }
        }

        //平滑内部边界并固定最远覆盖
        if (previousSplits && std::isfinite(previousSplits[index]) && previousSplits[index] > n && index + 1 < settings.count)
            split = std::exp(std::log(previousSplits[index]) * (1.0f - weight) + std::log(split) * weight);
        float32 minimum = index == 0 ? n + gap : splits[index - 1] + gap;
        splits[index] = index + 1 == settings.count ? f : std::clamp(split, minimum, f - gap * (settings.count - index - 1));
    }
}

ShadowCascade ShadowCascadeBuilder::BuildCascade(const matrix4x4& cameraWorld, const matrix4x4& projection,
    const vector3& lightDirection, float32 sliceNear, float32 sliceFar,
    const ShadowCascadeSettings& source, const List<bounds3>& casterBounds)
{
    const ShadowCascadeSettings settings = ValidateSettings(source);
    matrix4x4 inverseProjection = RenderMath::Inverse(projection);
    vector3 corners[8];
    vector3 center;
    //还原相机空间分区角点
    for (int32 index = 0; index < 4; ++index)
    {
        vector3 ray = RenderMath::TransformPoint(inverseProjection,
            { (index & 1) ? 1.0f : -1.0f, (index & 2) ? 1.0f : -1.0f, 0.0f });
        float32 depth = std::max(-ray.z, 0.000001f);
        for (int32 end = 0; end < 2; ++end)
        {
            float32 scale = (end ? sliceFar : sliceNear) / depth;
            vector3 point{ ray.x * scale, ray.y * scale, ray.z * scale };
            corners[index + end * 4] = point;
            center.x += point.x / 8.0f;
            center.y += point.y / 8.0f;
            center.z += point.z / 8.0f;
        }
    }
    float32 radius = 0.0f;
    for (const vector3& point : corners)
    {
        vector3 offset{ point.x - center.x, point.y - center.y, point.z - center.z };
        radius = std::max(radius, std::sqrt(RenderMath::Dot(offset, offset)));
    }
    radius = std::max(std::ceil(radius * 16.0f) / 16.0f, 0.0625f);
    center = RenderMath::TransformPoint(cameraWorld, center);

    //建立固定太阳坐标基
    vector3 direction = RenderMath::Normalize(lightDirection);
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        RenderMath::Dot(direction, direction) < 0.5f)
        direction = RenderMath::Normalize({ -0.35f, -1.0f, -0.45f });
    vector3 up = std::abs(direction.y) > 0.9f ? vector3{ 0.0f, 0.0f, 1.0f } : vector3{ 0.0f, 1.0f, 0.0f };
    matrix4x4 lightView = RenderMath::LookAt({}, direction, up);
    vector3 lightCenter = RenderMath::TransformPoint(lightView, center);
    float32 halfSize = radius / (1.0f - 8.0f / settings.resolution);
    float32 texel = 2.0f * halfSize / settings.resolution;
    float32 x = std::floor(lightCenter.x / texel + 0.5f) * texel;
    float32 y = std::floor(lightCenter.y / texel + 0.5f) * texel;
    float32 minZ = lightCenter.z - radius;
    float32 maxZ = lightCenter.z + radius;

    //收集屏幕外上游遮挡范围
    for (const bounds3& bounds : casterBounds)
    {
        if (!bounds.valid) continue;
        bounds3 projected = RenderMath::TransformBounds(lightView, bounds);
        if (projected.center.x + projected.extents.x < x - halfSize || projected.center.x - projected.extents.x > x + halfSize ||
            projected.center.y + projected.extents.y < y - halfSize || projected.center.y - projected.extents.y > y + halfSize ||
            projected.center.z + projected.extents.z < minZ) continue;
        maxZ = std::max(maxZ, projected.center.z + projected.extents.z);
    }
    float32 padding = std::max(0.01f, texel * (settings.normalBias + 1.0f) + settings.depthBias);
    minZ -= padding;
    maxZ += padding;
    matrix4x4 ortho = RenderMath::Orthographic(x - halfSize, x + halfSize, y - halfSize, y + halfSize, -maxZ, -minZ);
    ShadowCascade cascade;
    cascade.worldToShadow = RenderMath::Mul(ortho, lightView);
    cascade.lightFrustum = RenderMath::BuildFrustum(cascade.worldToShadow);
    cascade.texelWorldSize = texel;
    cascade.depthRange = maxZ - minZ;
    cascade.splitDepth = sliceFar;
    return cascade;
}
