#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/RenderScene.h"

//Forward 渲染管线，负责组织阴影、天空盒和场景几何的绘制顺序。
class ForwardPipeline
{
private:
    //后端接口，用于创建资源并提交绘制命令。
    RenderBackend* backend = nullptr;

    //管线内置 shader，分别用于阴影深度和天空盒绘制。
    Ref<Shader> shadowDepthShader;
    Ref<Shader> skyboxShader;

    //阴影 pass 使用的深度贴图和渲染目标。
    GpuDepthTextureID shadowDepthTexture;
    GpuRenderTargetID shadowRenderTarget;

    //内置天空盒立方体网格。
    GpuMesh skyboxMesh;

    //当前帧方向光使用的世界到光源裁剪空间矩阵。
    matrix4x4 lightViewProjection;

    //当前帧是否已经成功准备阴影资源。
    bool shadowReady = false;

    //阴影贴图边长；固定尺寸可以避免随窗口变化频繁重建资源。
    int32 shadowMapSize = 1024;

public:
    //初始化后端引用和管线内置资源。
    void Initialize(RenderBackend* renderBackend);

    //释放管线持有的所有内置 GPU 资源。
    void Shutdown();

    //准备当前帧共享的阴影资源和方向光矩阵。
    void PrepareFrame(const RenderScene& scene, GpuResourceManager& resources);

    //按照既定 pass 顺序渲染指定相机的可见集合。
    void Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& resources);

private:
    //检查后端状态，并确保阴影贴图和渲染目标已经创建。
    bool PrepareShadowResources();

    //检查后端状态，并确保天空盒 cube mesh 已创建。
    bool PrepareSkyboxMesh();

    //使用方向光视角绘制会投射阴影的几何深度。
    bool RenderShadowPass(const RenderScene& scene, const RenderDirectionalLight& light, const matrix4x4& lightViewProjection, GpuResourceManager& resources);

    //在当前相机的 color pass 中绘制全局天空盒。
    void RenderSkybox(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& resources);
};
