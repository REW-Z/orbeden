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
        //对外暴露的逻辑 ID，以及后端实际持有的资源句柄。
        RenderTargetID id;
        GpuDepthTextureID depthTexture;
        GpuRenderTargetID renderTarget;

        //离屏目标的实际尺寸，用于解析归一化 viewport。
        int32 width = 0;
        int32 height = 0;
    };

    //窗口提供的 framebuffer 尺寸和生命周期入口。
    IWindow* window = nullptr;

    //OpenGL 后端，负责与图形 API 交互。
    OpenGLRenderBackend backend;
    //GPU 资源管理器，负责按 CPU 对象缓存上传结果。
    GpuResourceManager resources;

    //Forward 管线，负责阴影、天空盒和场景几何绘制。
    ForwardPipeline forwardPipeline;
    //ImGui 覆盖层，负责渲染系统级 UI。
    ImGuiLayer imguiLayer;

    //从 World 构建当前帧渲染场景。
    RenderSceneBuilder sceneBuilder;
    //缓存实体空间变换，避免每帧重复递归计算。
    SpaceCache spaceCache;

    //当前帧的相机、灯光和绘制项。
    RenderScene scene;
    //按相机视锥和绘制层生成可见项。
    SceneCuller culler;
    //对可见项执行队列和距离排序。
    RenderItemSorter sorter;

    //由外部设置的额外覆盖层。
    IRenderOverlay* renderOverlay = nullptr;

    //复用的单相机可见集合，避免逐相机分配临时容器。
    VisibleSet visibleSet;

    //运行时创建的离屏渲染目标及其后端资源。
    List<ManagedRenderTarget> renderTargets;
    uint32 nextRenderTargetId = 1;

    //主 framebuffer 尺寸。
    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;

    //渲染系统运行状态和一次性警告状态。
    bool initialized = false;
    bool warnedMissingCamera = false;
    bool fpsLabelVisible = true;

    //在当前 framebuffer 上绘制渲染系统级调试 UI。
    void RenderDebugOverlay();

    //按逻辑 ID 查找离屏渲染目标。
    ManagedRenderTarget* FindRenderTarget(RenderTargetID id);
    const ManagedRenderTarget* FindRenderTarget(RenderTargetID id) const;

    //释放全部离屏渲染目标及其深度资源。
    void ReleaseRenderTargets();

    //为当前帧相机解析目标、像素 viewport 和投影相关数据。
    void ResolveCameraTargets();

public:
    //初始化窗口、后端、资源缓存和内置渲染管线。
    bool Initialize(IWindow* renderWindow);

    //按资源依赖顺序关闭渲染系统并释放所有资源。
    void Shutdown();

    //设置额外的渲染覆盖层；对象生命周期由调用方管理。
    void SetRenderOverlay(IRenderOverlay* overlay);

    //设置是否在调试覆盖层中绘制 FPS 标签。
    void SetFpsLabelVisible(bool value);

    //创建带深度缓冲的离屏渲染目标并返回逻辑 ID。
    RenderTargetID CreateRenderTarget(int32 width, int32 height);

    //以新尺寸重建离屏渲染目标的后端资源。
    bool ResizeRenderTarget(RenderTargetID id, int32 width, int32 height);

    //删除离屏渲染目标及其关联的深度纹理。
    void DeleteRenderTarget(RenderTargetID id);

    //获取离屏目标的颜色纹理，供其他系统采样或显示。
    GpuTextureID GetRenderTargetTexture(RenderTargetID id) const;

    /// <summary>获取仅供当前帧覆盖层只读访问的渲染场景。</summary>
    const RenderScene& GetCurrentScene() const;

    //准备切换项目，释放当前 GPU 上传缓存和内置管线资源。
    void PrepareProjectReload();

    //项目切换完成后重新初始化内置管线资源。
    void CompleteProjectReload();

    //构建并渲染当前世界的一帧。
    void Render(World& world, float deltaTime) override;

    //响应窗口 framebuffer 尺寸变化；具体 viewport 在下一帧解析。
    void OnWindowResize(int width, int height) override;
};
