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

    //内置 shader 是否需要从当前内容根目录重新加载。
    bool builtinShadersInvalidated = true;

    //阴影贴图边长；固定尺寸可以避免随窗口变化频繁重建资源。
    int32 shadowMapSize = 1024;

public:
    //初始化后端引用并等待首次绘制时加载内置 shader。
    void Initialize(RenderBackend* renderBackend);

    //释放资源相关状态并保留当前渲染后端。
    void InvalidateResourceCaches();

    //释放管线持有的所有内置 GPU 资源。
    void Shutdown();

    //准备当前帧共享的阴影资源和方向光矩阵。
    void PrepareFrame(const RenderScene& scene, GpuResourceManager& gpuResourceManager);

    //按照既定 pass 顺序渲染指定相机的可见集合。
    void Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& gpuResourceManager);

private:
    //计算所有有效渲染器世界包围盒的整体中心。
    vector3 CalculateSceneCenter(const RenderScene& scene) const;

    //根据方向光和场景范围构造光源视图投影矩阵。
    matrix4x4 CalculateLightViewProjection(const RenderScene& scene, const RenderDirectionalLight& light) const;

    //从当前内容根目录加载管线内置 shader。
    void LoadBuiltinShaders();

    //检查后端状态，并确保阴影贴图和渲染目标已经创建。
    bool PrepareShadowResources();

    //检查后端状态，并确保天空盒 cube mesh 已创建。
    bool PrepareSkyboxMesh();

    //使用方向光视角绘制会投射阴影的几何深度。
    bool RenderShadowPass(const RenderScene& scene, const RenderDirectionalLight& light, const matrix4x4& lightViewProjection, GpuResourceManager& gpuResourceManager);

    //按指定队列绘制当前相机的可见项。
    void RenderQueueItems(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& gpuResourceManager, DrawQueue drawQueue, const RenderDirectionalLight* mainLight, bool cameraTexturesReady);

    //在当前相机的 color pass 中绘制全局天空盒。
    void RenderSkybox(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& gpuResourceManager);
};
