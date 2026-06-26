#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/RenderScene.h"

//Forward 渲染管线
class ForwardPipeline
{
private:
    RenderBackend* backend = nullptr;
    Shader* shadowDepthShader = nullptr;
    Shader* skyboxShader = nullptr;
    GpuDepthTextureID shadowDepthTexture;
    GpuRenderTargetID shadowRenderTarget;
    GpuMesh skyboxMesh;
    int32 shadowMapSize = 1024;

public:
    //初始化管线
    void Initialize(RenderBackend* renderBackend);

    //释放管线持有的内置资源
    void Shutdown();

    //渲染可见集合
    void Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& resources);

private:
    //确保阴影贴图和渲染目标已创建
    bool EnsureShadowResources();

    //确保天空盒 cube mesh 已创建
    bool EnsureSkyboxMesh();

    //绘制阴影深度 pass
    bool RenderShadowPass(const RenderScene& scene, const RenderDirectionalLight& light, const matrix4x4& lightViewProjection, GpuResourceManager& resources);

    //绘制全局天空盒
    void RenderSkybox(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& resources);
};
