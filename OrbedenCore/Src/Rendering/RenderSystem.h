#pragma once

#include "Application.h"
#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/ForwardPipeline.h"
#include "Rendering/DebugLineRenderer.h"
#include "Rendering/ImGuiLayer.h"
#include "Rendering/OutputPass.h"
#include "Rendering/InstanceDrawData.h"
#include "Rendering/RenderItemSorter.h"
#include "Rendering/SceneCuller.h"
#include "Rendering/TransformCache.h"
#include "Runtime/Gui/UIRenderer.h"

#include <span>
#include <unordered_map>

//渲染覆盖层接口，由渲染系统统一管理 frame 生命周期。
class IRenderOverlay
{
public:
    virtual ~IRenderOverlay() = default;

    //绘制一帧覆盖层内容
    virtual void DrawOverlay() = 0;
};

//相机场景绘制与颜色输出前的渲染扩展
class ICameraRenderExtension
{
public:
    virtual ~ICameraRenderExtension() = default;

    //替换绑定相机的场景几何绘制
    virtual void RenderCameraScene(RenderBackend& backend, GpuResourceManager& gpuResourceManager,
        World& world, const RenderCamera& camera, const VisibleSet& visibleSet,
        std::span<const InstanceSubmission> submissions) {}

    /// <summary>向绑定相机的线性场景缓冲绘制扩展内容。</summary>
    virtual void RenderCameraEffects(RenderBackend& backend, GpuResourceManager& gpuResourceManager,
        World& world, const RenderCamera& camera, const VisibleSet& visibleSet) = 0;

    /// <summary>在解除绑定或后端关闭前释放 GPU 资源。</summary>
    virtual void ReleaseResources(RenderBackend& backend) = 0;
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

    //每相机一块引擎管理的场景缓冲，外加折射用的颜色与深度快照。
    //两者尺寸都跟随相机视口，视口变化时整体重建。
    struct ManagedCameraFrameTextures
    {
        EnsId cameraEns;

        //场景缓冲：几何与光照的绘制目标，RGBA16F 线性 HDR。
        GpuRenderTargetID sceneRenderTarget;
        GpuDepthTextureID sceneDepthTexture;
        GpuTextureID sceneColorTexture;

        //折射快照：透明队列之后从场景缓冲复制而来。
        GpuDepthTextureID depthTexture;
        GpuRenderTargetID renderTarget;

        int32 width = 0;
        int32 height = 0;
        bool active = false;
    };

    //窗口提供的 framebuffer 尺寸和生命周期入口
    IWindow* window = nullptr;
    World* debugLineWorld = nullptr;
    List<DebugLine> debugLines;

    //活动渲染后端
    std::unique_ptr<RenderBackend> backend;
    DebugLineRenderer debugLineRenderer;
    //GPU 资源管理器
    GpuResourceManager gpuResourceManager;

    //Forward 管线
    ForwardPipeline forwardPipeline;
    //输出 Pass：把线性场景缓冲转换到显示目标，管线的末端
    OutputPass outputPass;
    //ImGui 覆盖层
    ImGuiLayer imguiLayer;
    //RetainedGUI 渲染器：屏幕、世界空间与离屏画布都经它提交
    UIRenderer uiRenderer;

    //缓存实体变换
    TransformCache transformCache;

    //持久渲染场景
    RenderScene scene;

    //剔除器
    SceneCuller culler;

    //排序  
    RenderItemSorter sorter;

    //由外部设置的额外覆盖层
    IRenderOverlay* renderOverlay = nullptr;

    struct CameraRenderExtension
    {
        ICameraRenderExtension* extension = nullptr;
        bool replaceSceneRendering = false;
    };

    //按完整相机句柄登记的扩展
    std::unordered_map<uint64, CameraRenderExtension> cameraRenderExtensions;

    //编辑器预览目标；未设置时屏幕画布画主帧缓冲。
    UIOutputTarget editorPreviewTarget;
    bool hasEditorPreviewTarget = false;

    //复用的单相机可见集合
    VisibleSet visibleSet;

    //运行时创建的离屏渲染目标及其后端资源
    List<ManagedRenderTarget> renderTargets;
    List<ManagedCameraFrameTextures> cameraFrameTextures;
    uint32 nextRenderTargetId = 1;

    //主 framebuffer 尺寸。
    int32 framebufferWidth = 0;
    int32 framebufferHeight = 0;

    //状态
    bool initialized = false;
    bool warnedMissingCamera = false;
    bool fpsLabelVisible = true;

    //是否渲染直接画到主 framebuffer 的相机（游戏相机）；编辑器编辑态关闭，Play 与 Player 打开
    bool mainFramebufferRendering = true;
    float32 elapsedTime = 0.0f;

    //显式实例提交：Submit 写入 pending，Render 开始时整体换入 active 并清空 pending。
    //暂停时没有新 Submit 就不会重复上一帧的内容。
    List<InstanceSubmission> pendingInstanceSubmissions;
    List<InstanceSubmission> activeInstanceSubmissions;
    //本帧已经接收的累计实例数量，用于执行每帧总预算
    uint64 pendingSubmittedInstanceCount = 0;
    uint64 nextInstanceSubmissionId = 1;
    //从消费 active 到本帧结束为 true，期间拒绝新的 Submit
    bool readingDrawSubmissions = false;

    //清空本帧使用的显式提交并解除读取状态
    void FinishDrawSubmissions();

    //验证一份实例数据是否可以使用，失败时给出诊断
    bool ValidateInstanceSubmission(Mesh* mesh, Material* material, uint32 subMeshIndex,
        std::span<const MeshInstanceData> instances) const;

    //在主 framebuffer 上绘制运行时 GUI 和调试覆盖层
    void RenderOverlayPass();

    //按逻辑 ID 查找离屏渲染目标
    ManagedRenderTarget* FindRenderTarget(RenderTargetID id);
    const ManagedRenderTarget* FindRenderTarget(RenderTargetID id) const;

    //释放全部离屏渲染目标及其深度资源
    void ReleaseRenderTargets();

    //释放所有相机颜色和深度快照资源
    void ReleaseCameraFrameTextures();

    //准备当前帧相机的目标、像素 viewport 和投影数据
    void PrepareCameraRenderData();

    //把完成相机绘制的线性场景缓冲转换到最终输出目标
    void RenderOutputPass(const RenderCamera& camera);

    //查找指定相机持有的颜色和深度快照资源
    ManagedCameraFrameTextures* FindCameraFrameTextures(EnsId cameraEns);

public:

    //RetainedGUI 渲染器：内容根切换后需要让它重建着色器程序。
    UIRenderer& GetUIRenderer() { return uiRenderer; }
    /// <summary>获取活动渲染系统。</summary>
    static RenderSystem* Current();

    /// <summary>命中快照的深度回读；只读已经呈现的那一帧。</summary>
    static bool ReadUIDepth(uint64 viewId, uint64 viewerId, uint64 presentedFrame,
        int32 x, int32 y, float32* depth);

    /// <summary>提交当前帧世界空间线条；全部相机绘制完成后清除。</summary>
    void DrawLine(World& world, const vector3& start, const vector3& end, const color& tint,
        bool depthTest = false, uint32 drawLayer = 1u);

    //获取活动渲染后端
    RenderBackend& GetBackend() { return *backend; }
    //提交主窗口显示
    void Present();

    //获取资源依赖并初始化窗口渲染后端
    bool OnInitialize(Application& app) override;

    //关闭并释放渲染系统
    void OnShutdown() override;

    //初始化
    bool Initialize(IWindow* renderWindow, std::unique_ptr<RenderBackend> initializedBackend = nullptr);

    //切换窗口后端并重建离屏目标，保留逻辑目标 ID
    bool ChangeWindow(IWindow* renderWindow);

    //按资源依赖顺序关闭渲染系统并释放所有资源
    void Shutdown();

    //设置额外的渲染覆盖层
    void SetRenderOverlay(IRenderOverlay* overlay);

    /// <summary>绑定指定相机的渲染扩展，传 nullptr 解除绑定；实例由调用方持有。</summary>
    void SetCameraRenderExtension(EnsId cameraEns, ICameraRenderExtension* extension,
        bool replaceSceneRendering = false);

    //设置是否在调试覆盖层中绘制 FPS 标签  
    void SetFpsLabelVisible(bool value);

    //编辑器预览：屏幕画布改画进场景面板的离屏目标，而不是主帧缓冲。
    void SetUIEditorPreviewTarget(const UIOutputTarget& target);

    //按逻辑目标查找场景面板的 GPU 帧缓冲
    void SetUIEditorPreviewTarget(RenderTargetID target);

    //撤销编辑器预览目标，屏幕画布回到主帧缓冲。
    void ClearUIEditorPreviewTarget();

    //设置是否渲染直接画到主 framebuffer 的相机  
    void SetMainFramebufferRendering(bool value);

    //创建带深度缓冲的离屏渲染目标并返回逻辑 ID  
    RenderTargetID CreateRenderTarget(int32 width, int32 height);

    //以新尺寸重建离屏渲染目标的后端资源  
    bool ResizeRenderTarget(RenderTargetID id, int32 width, int32 height);

    //删除离屏渲染目标及其关联的深度纹理  
    void DeleteRenderTarget(RenderTargetID id);

    //获取离屏目标的颜色纹理
    GpuTextureID GetRenderTargetTexture(RenderTargetID id) const;

    //取一张引擎纹理的 GPU 句柄；编辑器把纹理画进面板时用。
    GpuTextureID GetTextureId(Texture2D* texture);

    //获取离屏目标的深度纹理，供后处理共享同一份深度
    GpuDepthTextureID GetRenderTargetDepthTexture(RenderTargetID id) const;

    const RenderScene& GetCurrentScene() const;

    /// <summary>读取最近一帧的渲染批次统计，每帧开始时清零。</summary>
    const RenderBatchStats& GetBatchStats() const { return forwardPipeline.GetBatchStats(); }

    //切换普通网格的 CPU 动态合批
    void SetMeshDynamicBatchingEnabled(bool value) { forwardPipeline.SetMeshDynamicBatchingEnabled(value); }

    //读取普通网格的 CPU 动态合批开关
    bool IsMeshDynamicBatchingEnabled() const { return forwardPipeline.IsMeshDynamicBatchingEnabled(); }

    /// <summary>接收一次显式实例提交；复制配置与实例，下一次 Render 绘制。</summary>
    bool SubmitInstances(World& world, const InstanceDrawOptions& options, Mesh* mesh, uint32 subMeshIndex,
        Material* material, std::span<const MeshInstanceData> instances, int32 sourceObjectId = 0);

    /// <summary>读取本次 Render 使用的显式实例提交快照。</summary>
    const List<InstanceSubmission>& GetActiveInstanceSubmissions() const { return activeInstanceSubmissions; }

    //使内容资源相关的 GPU 缓存和管线状态失效
    void InvalidateResourceCaches();

    //渲染当前世界
    void Render(World& world, float deltaTime);

    //响应窗口尺寸变化
    void OnWindowResize(int width, int height) override;
};
