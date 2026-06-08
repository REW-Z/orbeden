#pragma once

#include "Application.h"
#include "Rendering/Backend/OpenGLRenderBackend.h"
#include "Rendering/ForwardPipeline.h"
#include "Rendering/RenderItemSorter.h"
#include "Rendering/RenderSceneBuilder.h"
#include "Rendering/SceneCuller.h"

//渲染系统
class RenderSystem : public IEngineSystem
{
private:
    IWindow* window = nullptr;
    OpenGLRenderBackend backend;
    GpuResourceManager resources;
    ForwardPipeline forwardPipeline;
    RenderSceneBuilder sceneBuilder;
    SpaceCache spaceCache;
    SceneCuller culler;
    RenderItemSorter sorter;
    RenderScene scene;
    VisibleSet visibleSet;
    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;
    bool initialized = false;
    bool warnedMissingCamera = false;

public:
    //初始化渲染系统
    bool Initialize(IWindow* renderWindow);

    //关闭渲染系统
    void Shutdown();

    //渲染当前世界
    void Render(World& world, float deltaTime) override;

    //响应窗口尺寸变化
    void OnWindowResize(int width, int height) override;
};

