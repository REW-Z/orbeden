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
    struct ManagedRenderTarget
    {
        RenderTargetID id;
        GpuDepthTextureID depthTexture;
        GpuRenderTargetID renderTarget;
        int32 width = 0;
        int32 height = 0;
    };

    IWindow* window = nullptr;

    //OpenGL后端
    OpenGLRenderBackend backend;
    //GPU资源管理器
    GpuResourceManager resources;

    //Forward管线
    ForwardPipeline forwardPipeline;
    //IMGUI覆盖层
    ImGuiLayer imguiLayer;

    //从 World 构建当前帧渲染场景
    RenderSceneBuilder sceneBuilder;
    //空间矩阵缓存  
    SpaceCache spaceCache;

    //渲染场景
    RenderScene scene;
    //剔除器
    SceneCuller culler;
    //渲染排序
    RenderItemSorter sorter;

	//覆盖层
    IRenderOverlay* renderOverlay = nullptr;

    //可见集
    VisibleSet visibleSet;

    //运行时离屏渲染目标
    List<ManagedRenderTarget> renderTargets;
    uint32 nextRenderTargetId = 1;

    //FB尺寸
    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;

    //状态
    bool initialized = false;
    bool warnedMissingCamera = false;
    bool fpsLabelVisible = true;

    //绘制渲染系统级调试 UI
    void RenderDebugOverlay();

    //查找离屏渲染目标
    ManagedRenderTarget* FindRenderTarget(RenderTargetID id);
    const ManagedRenderTarget* FindRenderTarget(RenderTargetID id) const;

    //释放全部离屏渲染目标
    void ReleaseRenderTargets();

    //为帧相机解析目标和 viewport
    void ResolveCameraTargets();

public:
    //初始化渲染系统
    bool Initialize(IWindow* renderWindow);

    //关闭渲染系统
    void Shutdown();

    //设置额外的渲染覆盖层
    void SetRenderOverlay(IRenderOverlay* overlay);

    //设置是否绘制 FPS 标签
    void SetFpsLabelVisible(bool value);

    //创建离屏渲染目标
    RenderTargetID CreateRenderTarget(int32 width, int32 height);

    //调整离屏渲染目标尺寸
    bool ResizeRenderTarget(RenderTargetID id, int32 width, int32 height);

    //删除离屏渲染目标
    void DeleteRenderTarget(RenderTargetID id);

    //获取离屏目标颜色纹理
    GpuTextureID GetRenderTargetTexture(RenderTargetID id) const;

    //准备切换项目，释放当前 GPU 上传缓存和内置管线资源
    void PrepareProjectReload();

    //项目切换完成后重新初始化内置管线资源
    void CompleteProjectReload();

    //渲染当前世界
    void Render(World& world, float deltaTime) override;

    //响应窗口尺寸变化
    void OnWindowResize(int width, int height) override;
};
