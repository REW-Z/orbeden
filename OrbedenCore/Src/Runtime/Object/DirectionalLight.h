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
    //使用 美术强度刻度，默认 1，允许大于 1。
    //原样传入 Shader，漫反射采用单位响应，镜面采用配套归一化。
    float32 intensity = 1.0f;
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
