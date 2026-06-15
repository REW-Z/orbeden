#pragma once

#include "Application.h"
#include "Rendering/Backend/OpenGLRenderBackend.h"
#include "Rendering/ForwardPipeline.h"
#include "Rendering/ImGuiLayer.h"
#include "Rendering/RenderItemSorter.h"
#include "Rendering/RenderSceneBuilder.h"
#include "Rendering/SceneCuller.h"

//渲染覆盖层接口，由渲染系统统一管理 frame 生命周期。
class IRenderOverlay
{
public:
    virtual ~IRenderOverlay() = default;

    //绘制一帧覆盖层内容
    virtual void DrawOverlay() = 0;
};

//渲染系统
class RenderSystem : public IEngineSystem
{
private:
    IWindow* window = nullptr;
    OpenGLRenderBackend backend;
    GpuResourceManager resources;
    ForwardPipeline forwardPipeline;
    ImGuiLayer imguiLayer;
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
    bool fpsLabelVisible = true;
    IRenderOverlay* renderOverlay = nullptr;

    //绘制渲染系统级调试 UI
    void RenderDebugOverlay();

public:
    //初始化渲染系统
    bool Initialize(IWindow* renderWindow);

    //关闭渲染系统
    void Shutdown();

    //设置额外的渲染覆盖层
    void SetRenderOverlay(IRenderOverlay* overlay);

    //设置是否绘制 FPS 标签
    void SetFpsLabelVisible(bool value);

    //准备切换项目，释放当前 GPU 上传缓存和内置管线资源
    void PrepareProjectReload();

    //项目切换完成后重新初始化内置管线资源
    void CompleteProjectReload();

    //渲染当前世界
    void Render(World& world, float deltaTime) override;

    //响应窗口尺寸变化
    void OnWindowResize(int width, int height) override;
};
