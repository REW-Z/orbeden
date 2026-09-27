#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/Object/Component.h"
#include "Runtime/EngineTypes.h"

//方向光组件，描述全局平行光和基础阴影参数。
class DirectionalLight : public Component
{
    OBJECT_TYPE_DECLARE(DirectionalLight)

private:
    bool enabled = true;

    //按当前状态同步渲染场景注册
    void SyncRenderSceneRegistration();

public:
    //光照方向不在这里：它与太阳的朝向是同一个东西，直接取所属 Ens 的 Transform 前向，
    //用旋转手柄转太阳即可。见 RenderScene 生成方向光快照的地方。
    color color = { 1.0f, 0.96f, 0.86f, 1.0f };
    //强度的单位是辐亮度，不是"亮度倍数"：白色表面正对太阳、nDotL = 1 时光照结果是
    //intensity/π，因此 intensity = π 恰好得到 1.0 线性亮度，即满日照。
    //Unity 那类美术友好的约定习惯填 1.0，换算到这里要乘 π。
    float32 intensity = 3.14159265f;
    bool castShadows = true;
    //世界单位的深度偏移
    float32 shadowBias = 0.0005f;
    float32 shadowStrength = 0.45f;
    float32 shadowDistance = 20000.0f;
    int32 shadowCascadeCount = 4;
    int32 shadowMapResolution = 2048;
    float32 shadowSplitLambda = 1.0f;
    bool shadowAdaptive = true;
    float32 shadowNormalBias = 0.25f;
    float32 shadowBlendRatio = 0.1f;
    int32 shadowDebugView = 0;

    //获取启用状态
    bool GetEnabled() const;

    //设置启用状态并同步渲染场景注册
    void SetEnabled(bool value);

    //判断当前组件是否应注册到渲染场景
    bool IsRenderSceneEligible() const;

    //挂载时注册到当前渲染场景
    void OnAttach() override;

    //卸载时从当前渲染场景注销
    void OnDetach() override;

    //所属 Ens 的 worldActive 变化时同步渲染场景注册
    void OnWorldActiveChanged(bool worldActive) override;
};
