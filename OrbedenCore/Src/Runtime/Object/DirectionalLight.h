#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EnsId.h"
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
    vector3 direction = { -0.35f, -1.0f, -0.45f };
    color color = { 1.0f, 0.96f, 0.86f, 1.0f };
    float32 intensity = 1.2f;
    bool castShadows = true;
    float32 shadowBias = 0.004f;
    float32 shadowStrength = 0.45f;
    float32 shadowDistance = 24.0f;

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
