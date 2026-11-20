#include "Rendering/ShadowCascadeBuilder.h"
#include "Rendering/Math.h"
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
    settings.count = std::clamp(settings.count, 0, MaximumShadowCascades);
    int32 requested = std::clamp(settings.resolution, 256, 4096);
    settings.resolution = 256;
    while (settings.resolution * 2 <= requested) settings.resolution *= 2;
    settings.blendRatio = ClampFinite(settings.blendRatio, 0.0f, 0.3f, 0.1f);
    settings.depthBias = ClampFinite(settings.depthBias, 0.0f, 10.0f, 0.0005f);
    settings.normalBias = ClampFinite(settings.normalBias, 0.0f, 4.0f, 0.25f);
    return settings;
}

//裁剪手动结束距离并跳过空区间
int32 ShadowCascadeBuilder::BuildSplits(std::span<const float32> cascadeEnds, float32 nearPlane, float32 farPlane,
    std::span<float32> splits)
{
    if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0.0f || farPlane <= nearPlane)
    {
        return 0;
    }

    int32 count = 0;
    float32 start = nearPlane;
    for (float32 end : cascadeEnds.first(std::min<usize>(cascadeEnds.size(), MaximumShadowCascades)))
    {
        if (!std::isfinite(end) || end <= start)
        {
            continue;
        }
        if (static_cast<usize>(count) == splits.size())
        {
            break;
        }

        start = std::min(end, farPlane);
        splits[count++] = start;
        if (start == farPlane)
        {
            break;
        }
    }
    return count;
}

ShadowCascade ShadowCascadeBuilder::BuildCascade(const matrix4x4& cameraWorld, const matrix4x4& projection,
    const vector3& lightDirection, float32 sliceNear, float32 sliceFar,
    const ShadowCascadeSettings& source, const List<bounds3>& casterBounds)
{
    const ShadowCascadeSettings settings = ValidateSettings(source);
    matrix4x4 inverseProjection = Orbeden::Math::Inverse(projection);
    vector3 corners[8];
    vector3 center;
    //还原相机空间分区角点
    for (int32 index = 0; index < 4; ++index)
    {
        vector3 ray = Orbeden::Math::TransformPoint(inverseProjection,
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
        radius = std::max(radius, std::sqrt(Orbeden::Math::Dot(offset, offset)));
    }
    radius = std::max(std::ceil(radius * 16.0f) / 16.0f, 0.0625f);
    center = Orbeden::Math::TransformPoint(cameraWorld, center);

    //建立固定太阳坐标基
    vector3 direction = Orbeden::Math::Normalize(lightDirection);
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        Orbeden::Math::Dot(direction, direction) < 0.5f)
        direction = Orbeden::Math::Normalize({ -0.35f, -1.0f, -0.45f });
    vector3 up = std::abs(direction.y) > 0.9f ? vector3{ 0.0f, 0.0f, 1.0f } : vector3{ 0.0f, 1.0f, 0.0f };
    matrix4x4 lightView = Orbeden::Math::LookAt({}, direction, up);
    vector3 lightCenter = Orbeden::Math::TransformPoint(lightView, center);
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
        bounds3 projected = Orbeden::Math::TransformBounds(lightView, bounds);
        if (projected.center.x + projected.extents.x < x - halfSize || projected.center.x - projected.extents.x > x + halfSize ||
            projected.center.y + projected.extents.y < y - halfSize || projected.center.y - projected.extents.y > y + halfSize ||
            projected.center.z + projected.extents.z < minZ) continue;
        maxZ = std::max(maxZ, projected.center.z + projected.extents.z);
    }
    float32 padding = std::max(0.01f, texel * (settings.normalBias + 1.0f) + settings.depthBias);
    minZ -= padding;
    maxZ += padding;
    matrix4x4 ortho = Orbeden::Math::Orthographic(x - halfSize, x + halfSize, y - halfSize, y + halfSize, -maxZ, -minZ);
    ShadowCascade cascade;
    cascade.worldToShadow = Orbeden::Math::Mul(ortho, lightView);
    cascade.lightFrustum = Orbeden::Math::BuildFrustum(cascade.worldToShadow);
    cascade.texelWorldSize = texel;
    cascade.depthRange = maxZ - minZ;
    cascade.splitDepth = sliceFar;
    return cascade;
}
