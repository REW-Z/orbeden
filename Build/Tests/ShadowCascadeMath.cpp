#include "Rendering/ShadowCascadeBuilder.h"
#include "Rendering/RenderMath.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>

//验证级联边界与稳定投影
int main()
{
    ShadowCascadeSettings settings;
    float32 splits[6]{};
    ShadowCascadeBuilder::BuildSplits(settings, 0.1f, 20000.0f, nullptr, nullptr, 0.0f, splits);
    assert(splits[0] < 3.0f && splits[0] > 1.0f);
    assert(splits[3] == 20000.0f);
    for (int32 i = 1; i < 4; ++i) assert(splits[i] > splits[i - 1]);
    GpuDepthDistribution distribution;
    distribution.nearPlane = 0.1f;
    distribution.farPlane = 20000.0f;
    distribution.bins[60] = 10000;
    float32 adapted[6]{};
    ShadowCascadeBuilder::BuildSplits(settings, 0.1f, 20000.0f, &distribution, nullptr, 0.0f, adapted);
    assert(adapted[0] > splits[0] && adapted[3] == splits[3]);
    distribution.bins[60] = 0;
    ShadowCascadeBuilder::BuildSplits(settings, 0.1f, 20000.0f, &distribution, nullptr, 0.0f, adapted);
    for (int32 i = 0; i < 4; ++i) assert(adapted[i] == splits[i]);

    //验证非法配置修正
    settings.count = 99;
    settings.resolution = 1500;
    settings.splitLambda = std::numeric_limits<float32>::quiet_NaN();
    settings = ShadowCascadeBuilder::ValidateSettings(settings);
    assert(settings.count == 6 && settings.resolution == 1024 && settings.splitLambda == 1.0f);
    settings = {};
    matrix4x4 projection = RenderMath::Perspective(60.0f, 1.6f, 0.1f, 20000.0f);
    vector3 direction = RenderMath::Normalize({0.1f, -0.01f, -1.0f});
    auto cascade = ShadowCascadeBuilder::BuildCascade({}, projection, direction, 0.1f, 3.0f, settings, {});
    matrix4x4 rotation = RenderMath::LookAt({}, {1.0f, 0.0f, 0.0f}, {0.0f,1.0f,0.0f});
    auto rotated = ShadowCascadeBuilder::BuildCascade(rotation, projection, direction, 0.1f, 3.0f, settings, {});
    assert(std::abs(cascade.texelWorldSize - rotated.texelWorldSize) < 1e-7f);
    for (float32 value : cascade.worldToShadow.m) assert(std::isfinite(value));

    //验证视锥角点被级联覆盖
    matrix4x4 inverseProjection = RenderMath::Inverse(projection);
    for (int32 i=0;i<8;++i)
    {
        vector3 ray = RenderMath::TransformPoint(inverseProjection, {(i&1)?1.0f:-1.0f,(i&2)?1.0f:-1.0f,0.0f});
        float32 scale = ((i&4)?3.0f:0.1f) / -ray.z;
        vector3 point{ray.x*scale,ray.y*scale,ray.z*scale};
        vector3 projected = RenderMath::TransformPoint(cascade.worldToShadow,point);
        assert(std::abs(projected.x)<=1.00001f && std::abs(projected.y)<=1.00001f && std::abs(projected.z)<=1.00001f);
    }

    //验证远处上游投射物进入阴影视锥
    direction = {0.0f,-1.0f,0.0f};
    bounds3 caster{{0.0f,1000.0f,-1.5f},{0.1f,0.1f,0.1f},true};
    cascade = ShadowCascadeBuilder::BuildCascade({},projection,direction,0.1f,3.0f,settings,{caster});
    assert(RenderMath::Intersects(cascade.lightFrustum,caster));
    assert(cascade.depthRange>1000.0f);

    //验证亚纹素平移不改变光源 XY 网格
    matrix4x4 translated;
    translated.m[12]=cascade.texelWorldSize*0.1f;
    auto moved=ShadowCascadeBuilder::BuildCascade(translated,projection,direction,0.1f,3.0f,settings,{caster});
    assert(cascade.worldToShadow.m[12]==moved.worldToShadow.m[12]);
    std::cout << "Shadow cascade math tests passed.\n";
}
