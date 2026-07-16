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

        //离屏目标的实际尺寸 用于解析归一化viewport
        int32 width = 0;
        int32 height = 0;
    };

    //窗口提供的 framebuffer 尺寸和生命周期入口
    IWindow* window = nullptr;

    //OpenGL 后端
    OpenGLRenderBackend backend;
    //GPU 资源管理器
    GpuResourceManager resources;

    //Forward 管线
    ForwardPipeline forwardPipeline;
    //ImGui 覆盖层
    ImGuiLayer imguiLayer;

    //帧渲染场景构建  
    RenderSceneBuilder sceneBuilder;
    //缓存实体空间变换
    SpaceCache spaceCache;

    //当前帧的渲染场景
    RenderScene scene;

    //剔除器
    SceneCuller culler;

    //排序  
    RenderItemSorter sorter;

    //由外部设置的额外覆盖层
    IRenderOverlay* renderOverlay = nullptr;

    //复用的单相机可见集合
    VisibleSet visibleSet;

    //运行时创建的离屏渲染目标及其后端资源
    List<ManagedRenderTarget> renderTargets;
    uint32 nextRenderTargetId = 1;

    //主 framebuffer 尺寸。
    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;

    //状态
    bool initialized = false;
    bool warnedMissingCamera = false;
    bool fpsLabelVisible = true;

    //在当前 framebuffer 上绘制渲染系统级调试 UI
    void RenderDebugOverlay();

    //按逻辑 ID 查找离屏渲染目标
    ManagedRenderTarget* FindRenderTarget(RenderTargetID id);
    const ManagedRenderTarget* FindRenderTarget(RenderTargetID id) const;

    //释放全部离屏渲染目标及其深度资源
    void ReleaseRenderTargets();

    //为当前帧相机解析目标、像素 viewport 和投影相关数据
    void ResolveCameraTargets();

public:
    //初始化
    bool Initialize(IWindow* renderWindow);

    //按资源依赖顺序关闭渲染系统并释放所有资源
    void Shutdown();

    //设置额外的渲染覆盖层
    void SetRenderOverlay(IRenderOverlay* overlay);

    //设置是否在调试覆盖层中绘制 FPS 标签  
    void SetFpsLabelVisible(bool value);

    //创建带深度缓冲的离屏渲染目标并返回逻辑 ID  
    RenderTargetID CreateRenderTarget(int32 width, int32 height);

    //以新尺寸重建离屏渲染目标的后端资源  
    bool ResizeRenderTarget(RenderTargetID id, int32 width, int32 height);

    //删除离屏渲染目标及其关联的深度纹理  
    void DeleteRenderTarget(RenderTargetID id);

    //获取离屏目标的颜色纹理  
    GpuTextureID GetRenderTargetTexture(RenderTargetID id) const;

    const RenderScene& GetCurrentScene() const;

    //准备切换项目
    void PrepareProjectReload();

    //项目切换完成后重新初始化内置管线资源
    void CompleteProjectReload();

    //渲染
    void Render(World& world, float deltaTime) override;

    //响应窗口尺寸变化
    void OnWindowResize(int width, int height) override;
};
