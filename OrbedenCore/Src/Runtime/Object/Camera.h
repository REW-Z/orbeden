#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EnsId.h"

//相机组件，保存渲染视角的公开参数
class Camera : public Component
{
    OBJECT_TYPE_DECLARE(Camera)

private:
    bool enabled = true;

    //按当前状态同步渲染场景注册
    void SyncRenderSceneRegistration();

public:
    float32 fieldOfView = 60.0f;
    float32 nearPlane = 0.1f;
    float32 farPlane = 1000.0f;
    float32 depth = 0.0f;
    uint32 drawLayerMask = 0xFFFFFFFFu;
    ClearMode clearMode = ClearMode::SolidColor;
    color clearColor = { 0.1f, 0.12f, 0.16f, 1.0f };

    //归一化 viewport，坐标原点位于渲染目标左下角
    float32 viewportX = 0.0f;
    float32 viewportY = 0.0f;
    float32 viewportWidth = 1.0f;
    float32 viewportHeight = 1.0f;

    //0 表示绘制到默认窗口帧缓冲；离屏目标由 RenderSystem 在运行时分配
    uint32 renderTargetId = 0;

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
