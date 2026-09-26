#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/RenderScene.h"
#include "Rendering/CascadedShadowMap.h"

//Forward 渲染管线，负责组织阴影、天空盒和场景几何的绘制顺序。
class ForwardPipeline
{
private:
    //后端接口，用于创建资源并提交绘制命令。
    RenderBackend* backend = nullptr;

    //管线内置 shader，分别用于阴影深度和天空盒绘制。
    Ref<Shader> shadowDepthShader;
    Ref<Shader> skyboxShader;

    CascadedShadowMap shadows;

    //内置天空盒立方体网格。
    GpuMesh skyboxMesh;

    //内置 shader 是否需要从当前内容根目录重新加载。
    bool builtinShadersInvalidated = true;

public:
    //初始化后端引用并等待首次绘制时加载内置 shader。
    void Initialize(RenderBackend* renderBackend);

    //释放资源相关状态并保留当前渲染后端。
    void InvalidateResourceCaches();

    //释放管线持有的所有内置 GPU 资源。
    void Shutdown();

    //加载内置资源并清理相机阴影历史。
    void PrepareFrame(const RenderScene& scene, GpuResourceManager& gpuResourceManager);

    //按照既定 pass 顺序渲染指定相机的可见集合。
    void Render(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& gpuResourceManager);

    //阴影调试视图是否生效，供输出 Pass 决定要不要跳过色调映射。
    bool IsShadowDebugViewActive() const { return shadows.IsDebugViewActive(); }

private:
    //从当前内容根目录加载管线内置 shader。
    void LoadBuiltinShaders();

    //检查后端状态，并确保天空盒 cube mesh 已创建。
    bool PrepareSkyboxMesh();

    //按指定队列绘制当前相机的可见项。
    void RenderQueueItems(const RenderScene& scene, const VisibleSet& visibleSet, GpuResourceManager& gpuResourceManager, DrawQueue drawQueue, const RenderDirectionalLight* mainLight, bool cameraTexturesReady);

    //在当前相机的 color pass 中绘制全局天空盒。
    void RenderSkybox(const RenderScene& scene, const RenderCamera& camera, GpuResourceManager& gpuResourceManager);
};
