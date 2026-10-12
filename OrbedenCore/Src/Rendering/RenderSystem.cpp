#include "Rendering/RenderSystem.h"

#include "Log/Log.h"
#include "Profiler/Profiler.h"
#include "Rendering/ColorSpace.h"
#include "Rendering/RenderMath.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/DisplaySettings.h"
#include "Runtime/Object/Camera.h"

#include <algorithm>
#include <cmath>

namespace
{
    RenderSystem* currentRenderSystem = nullptr;
    //每次 Render 接收的显式实例总量上限。它与 GPU 单批上限是两件事，
    //超过这个数量的提交整体失败，之后按批切分。
    constexpr uint64 MaximumSubmittedInstances = 1048576u;
    //计算视口轴像素范围
    void CalculateViewportAxis(float32 normalizedStart, float32 normalizedSize, int32 targetSize, int32& start, int32& size)
    {
        //处理无效目标尺寸
        if (targetSize <= 0)
        {
            start = 0;
            size = 0;
            return;
        }

        //计算视口起点
        start = std::clamp(static_cast<int32>(normalizedStart * static_cast<float32>(targetSize)), 0, targetSize - 1);
        if (normalizedSize <= 0.0f)
        {
            size = 0;
            return;
        }

        //计算视口长度
        int32 end = std::clamp(static_cast<int32>((normalizedStart + normalizedSize) * static_cast<float32>(targetSize)), start + 1, targetSize);
        size = end - start;
    }

}

//获取资源依赖并初始化窗口渲染后端
bool RenderSystem::OnInitialize(Application& app)
{
    if (!app.GetSystem<ResourceManager>()) return false;

    IWindow* renderWindow = app.GetWindow();
    if (!renderWindow)
    {
        Log::Error("RenderSystem initialize failed: window is missing.");
        return false;
    }
    if (renderWindow->GetGraphicsApi() != WindowGraphicsApi::OpenGL)
    {
        Log::Error("RenderSystem initialize failed: graphics API is not supported.");
        return false;
    }

    if (!Initialize(renderWindow)) return false;

    scene.BindWorld(app.GetWorld());
    return true;
}

//关闭并释放渲染系统
void RenderSystem::OnShutdown()
{
    scene.UnbindWorld();
    Shutdown();
    renderOverlay = nullptr;
    cameraRenderExtensions.clear();
}

bool RenderSystem::Initialize(IWindow* renderWindow)
{
    //处理重复初始化
    if (initialized) return true;

    //验证渲染窗口
    if (!renderWindow)
    {
        Log::Error("RenderSystem initialize failed: window is missing.");
        return false;
    }

    //记录主帧缓冲尺寸
    window = renderWindow;
    framebufferWidth = window->GetFramebufferWidth();
    framebufferHeight = window->GetFramebufferHeight();

    //初始化渲染后端和管线
    if (!backend.Initialize(window))
    {
        Log::Error("RenderSystem initialize failed: backend initialize failed.");
        return false;
    }

    gpuResourceManager.Initialize(&backend);
    forwardPipeline.Initialize(&backend);
    outputPass.Initialize(&backend);
    //UI 渲染器失败不影响场景渲染：RetainedGUI 不显示，其余照常。
    if (!uiRenderer.Initialize(backend, gpuResourceManager))
    {
        Log::Warning("RenderSystem initialize warning: RetainedGUI renderer is disabled.");
    }
    //深度回读交给 UI 渲染器：它才知道每个视图画在哪块目标上。
    RetainedGuiFrame::SetDepthReader(&RenderSystem::ReadUIDepth);
    //UI 的命中换算需要窗口逻辑尺寸；帧缓冲尺寸每帧都可能变，逻辑尺寸在缩放时更新。
    uiRenderer.SetDisplayLogicalSize(vector2(static_cast<float32>(window->GetWidth()),
        static_cast<float32>(window->GetHeight())));
    elapsedTime = 0.0f;

    //初始化 ImGui 覆盖层
    if (!imguiLayer.Initialize(window))
    {
        Log::Warning("RenderSystem initialize warning: ImGui overlay is disabled.");
    }

    initialized = true;
    currentRenderSystem = this;
    return true;
}

void RenderSystem::Shutdown()
{
    if (currentRenderSystem == this) currentRenderSystem = nullptr;
    debugLines.clear();
    debugLineWorld = nullptr;
    //释放渲染系统资源
    imguiLayer.Shutdown();
    RetainedGuiFrame::SetDepthReader(nullptr);
    //UI 渲染器必须在资源管理器之前释放：它持有网格缓冲与覆盖率池。
    uiRenderer.Shutdown();
    //释放相机扩展资源
    for (const auto& entry : cameraRenderExtensions) entry.second->ReleaseResources(backend);
    ReleaseCameraFrameTextures();
    ReleaseRenderTargets();
    forwardPipeline.Shutdown();
    //输出 Pass 必须在后端关闭之前释放它持有的 shader 与顶点缓冲
    outputPass.Shutdown();
    gpuResourceManager.Shutdown();
    backend.Shutdown();
    initialized = false;
    window = nullptr;
    elapsedTime = 0.0f;
}

void RenderSystem::SetRenderOverlay(IRenderOverlay* overlay)
{
    renderOverlay = overlay;
}

/// <summary>绑定相机渲染扩展，替换或解除时释放原扩展资源。</summary>
void RenderSystem::SetCameraRenderExtension(EnsId cameraEns, ICameraRenderExtension* extension)
{
    if (cameraEns.IsNull()) return;

    uint64 cameraKey = (static_cast<uint64>(cameraEns.id) << 32) | cameraEns.version;
    auto found = cameraRenderExtensions.find(cameraKey);
    if (found != cameraRenderExtensions.end())
    {
        if (found->second == extension) return;
        found->second->ReleaseResources(backend);
        cameraRenderExtensions.erase(found);
    }
    if (extension) cameraRenderExtensions.emplace(cameraKey, extension);
}

void RenderSystem::SetUIEditorPreviewTarget(const UIOutputTarget& target)
{
    editorPreviewTarget = target;
    hasEditorPreviewTarget = target.width > 0 && target.height > 0;
}

void RenderSystem::ClearUIEditorPreviewTarget()
{
    hasEditorPreviewTarget = false;
}

//按逻辑目标配置 UI 预览帧缓冲
void RenderSystem::SetUIEditorPreviewTarget(RenderTargetID id)
{
    const ManagedRenderTarget* target = FindRenderTarget(id);
    if (!target)
    {
        ClearUIEditorPreviewTarget();
        return;
    }
    UIOutputTarget output;
    output.renderTarget = target->renderTarget;
    output.width = target->width;
    output.height = target->height;
    SetUIEditorPreviewTarget(output);
}

void RenderSystem::SetFpsLabelVisible(bool value)
{
    fpsLabelVisible = value;
}

//设置是否渲染直接画到主 framebuffer 的相机
void RenderSystem::SetMainFramebufferRendering(bool value)
{
    mainFramebufferRendering = value;
}

RenderTargetID RenderSystem::CreateRenderTarget(int32 width, int32 height)
{
    //验证 RenderTarget 创建参数
    if (!initialized || width <= 0 || height <= 0) return RenderTargetID();

    //创建深度纹理
    GpuDepthTextureDesc depthDesc;
    depthDesc.width = width;
    depthDesc.height = height;
    GpuDepthTextureID depthTexture = backend.CreateDepthTexture(depthDesc);
    if (!depthTexture.IsValid()) return RenderTargetID();

    //创建颜色渲染目标
    GpuRenderTargetDesc targetDesc;
    targetDesc.width = width;
    targetDesc.height = height;
    targetDesc.depthTexture = depthTexture;
    GpuRenderTargetID renderTarget = backend.CreateRenderTarget(targetDesc);
    if (!renderTarget.IsValid())
    {
        backend.DeleteDepthTexture(depthTexture);
        return RenderTargetID();
    }

    //生成 RenderTarget ID
    RenderTargetID id;
    do
    {
        id.id = nextRenderTargetId++;
        if (nextRenderTargetId == 0) nextRenderTargetId = 1;
    } while (!id.IsValid() || FindRenderTarget(id));

    //记录 RenderTarget 资源
    renderTargets.push_back({ id, depthTexture, renderTarget, width, height });
    return id;
}

bool RenderSystem::ResizeRenderTarget(RenderTargetID id, int32 width, int32 height)
{
    //验证 RenderTarget 尺寸
    ManagedRenderTarget* target = FindRenderTarget(id);
    if (!target || width <= 0 || height <= 0) return false;
    if (target->width == width && target->height == height) return true;

    //创建新尺寸 RenderTarget
    GpuDepthTextureDesc depthDesc;
    depthDesc.width = width;
    depthDesc.height = height;
    GpuDepthTextureID depthTexture = backend.CreateDepthTexture(depthDesc);
    if (!depthTexture.IsValid()) return false;

    GpuRenderTargetDesc targetDesc;
    targetDesc.width = width;
    targetDesc.height = height;
    targetDesc.depthTexture = depthTexture;
    GpuRenderTargetID renderTarget = backend.CreateRenderTarget(targetDesc);
    if (!renderTarget.IsValid())
    {
        backend.DeleteDepthTexture(depthTexture);
        return false;
    }

    //替换 RenderTarget 资源
    backend.DeleteRenderTarget(target->renderTarget);
    backend.DeleteDepthTexture(target->depthTexture);
    target->renderTarget = renderTarget;
    target->depthTexture = depthTexture;
    target->width = width;
    target->height = height;
    return true;
}

void RenderSystem::DeleteRenderTarget(RenderTargetID id)
{
    //查找待删除 RenderTarget
    auto it = std::find_if(renderTargets.begin(), renderTargets.end(), [id](const ManagedRenderTarget& target)
    {
        return target.id == id;
    });
    if (it == renderTargets.end()) return;

    //删除 RenderTarget 资源
    backend.DeleteRenderTarget(it->renderTarget);
    backend.DeleteDepthTexture(it->depthTexture);
    renderTargets.erase(it);
}

GpuTextureID RenderSystem::GetRenderTargetTexture(RenderTargetID id) const
{
    const ManagedRenderTarget* target = FindRenderTarget(id);
    return target ? backend.GetRenderTargetColorTexture(target->renderTarget) : GpuTextureID();
}

//获取持久渲染场景的只读视图
const RenderScene& RenderSystem::GetCurrentScene() const
{
    return scene;
}

void RenderSystem::InvalidateResourceCaches()
{
    if (!initialized) return;

    //释放内容资源 GPU 缓存
    forwardPipeline.InvalidateResourceCaches();
    gpuResourceManager.InvalidateCaches();
    //UI 的纹理引用可能已经失效，覆盖层状态一并重来。
    uiRenderer.InvalidateAll();
    warnedMissingCamera = false;
}

void RenderSystem::Render(World& world, float deltaTime)
{
    PROFILE("Render/Frame");

    //消费 Submit 快照：先换入 active 再清空 pending，之后的任何早退路径都不会把
    //实例积压到下一次 Render，也不会带进下一个项目。
    pendingInstanceSubmissions.swap(activeInstanceSubmissions);
    pendingInstanceSubmissions.clear();
    pendingSubmittedInstanceCount = 0;
    readingDrawSubmissions = true;

    if (!initialized || !window)
    {
        FinishDrawSubmissions();
        return;
    }

    //本帧批次统计从零开始，无相机与早退路径也会保留清零结果
    backend.ProfileGpuWait("Render/GpuWait/BeforeScene");
    forwardPipeline.ResetBatchStats();

    //累加 Shader 时间
    if (std::isfinite(deltaTime) && deltaTime > 0.0f)
    {
        elapsedTime += deltaTime;
    }

    //释放已销毁对象的 GPU 资源
    gpuResourceManager.ReleaseDestroyedResources();

    //刷新持久渲染场景
    {
        PROFILE("Render/SceneSync");
        scene.Update(world, transformCache);
    }

    //准备相机渲染数据
    PrepareCameraRenderData();

    scene.BeginRead();

    //开始渲染帧
    backend.BeginFrame();
    backend.ProfileGpuWait("Render/GpuWait/ResourcesAndTargets");


    //绘制无相机场景
    if (scene.cameras.empty())
    {
        //清空无相机场景
        if (!warnedMissingCamera)
        {
            Log::Warning("RenderSystem render warning: scene has no Camera.");
            warnedMissingCamera = true;
        }

        RenderPassDesc passDesc;
        passDesc.width = framebufferWidth;
        passDesc.height = framebufferHeight;
        passDesc.clearMode = ClearMode::SolidColor;
        passDesc.clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
        backend.BeginPass(passDesc);
        backend.EndPass();

        //无相机场景：离屏与屏幕画布照常绘制，只是没有世界空间画布。
        uiRenderer.RenderOffscreen();
        RenderOverlayPass();
        backend.EndFrame();
        scene.EndRead();
        debugLines.clear();
        debugLineWorld = nullptr;
        FinishDrawSubmissions();
        return;
    }

    //离屏画布在相机之前绘制：它们的结果可能被世界空间或屏幕画布采样。
    uiRenderer.RenderOffscreen();
    backend.ProfileGpuWait("Render/GpuWait/OffscreenUI");

    //准备共享阴影资源
    warnedMissingCamera = false;
    forwardPipeline.SetFrameWorld(&world);
    forwardPipeline.SetDrawSubmissions(&activeInstanceSubmissions);
    forwardPipeline.PrepareFrame(scene, gpuResourceManager);
    backend.ProfileGpuWait("Render/GpuWait/SharedResources");

    //绘制活动相机
    for (const RenderCamera& camera : scene.cameras)
    {
        if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) continue;

        //DepthOnly/None 的语义是保留显示目标已有的颜色。场景缓冲是每相机独立的，
        //首帧没有旧内容，必须先把显示目标当前内容搬进来，否则会画出未初始化的内存。
        if (camera.clearMode != ClearMode::SolidColor)
        {
            GpuRenderTargetCopyDesc seedDesc;
            seedDesc.sourceRenderTarget = camera.finalRenderTarget;
            seedDesc.destinationRenderTarget = camera.renderTarget;
            seedDesc.sourceX = camera.viewportX;
            seedDesc.sourceY = camera.viewportY;
            seedDesc.width = camera.viewportWidth;
            seedDesc.height = camera.viewportHeight;
            seedDesc.colorOnly = true;
            backend.CopyRenderTarget(seedDesc);
        }

        {
            PROFILE("Render/Cull");
            culler.Cull(scene, camera, visibleSet); //剔除
            scene.BuildRenderItems(visibleSet);
            sorter.Sort(visibleSet);//排序
        }

        {
            PROFILE("Render/Pipeline");
            forwardPipeline.Render(scene, visibleSet, gpuResourceManager);//forword绘制
        }

        //执行当前相机绑定的渲染扩展
        uint64 cameraKey = (static_cast<uint64>(camera.ens.id) << 32) | camera.ens.version;
        auto extension = cameraRenderExtensions.find(cameraKey);
        if (extension != cameraRenderExtensions.end())
        {
            extension->second->RenderCameraEffects(backend, gpuResourceManager, world, camera, visibleSet);
            backend.ProfileGpuWait("Render/GpuWait/CameraExtension");
        }
        if (debugLineWorld == &world && !debugLines.empty())
        {
            RenderPassDesc pass;
            //场景缓冲是视口尺寸且原点为 0，视口原点只在外层目标上才有意义
            pass.renderTarget = camera.renderTarget;
            pass.x = 0;
            pass.y = 0;
            pass.width = camera.viewportWidth;
            pass.height = camera.viewportHeight;
            pass.clearMode = ClearMode::None;
            backend.BeginPass(pass);
            backend.DrawLines(debugLines, camera.viewProjectionMatrix, camera.drawLayerMask);
            backend.EndPass();
        }

        //世界空间画布写在输出 Pass 之前，与场景一起被转换到显示空间。
        backend.ProfileGpuWait("Render/GpuWait/DebugLines");
        uiRenderer.RenderWorldSpace(camera);
        if (hasEditorPreviewTarget && camera.finalRenderTarget.id == editorPreviewTarget.renderTarget.id)
            uiRenderer.RenderEditorPreview(camera);
        backend.ProfileGpuWait("Render/GpuWait/WorldUI");

        //转换完成扩展绘制、调试线与世界 UI 的场景缓冲
        RenderOutputPass(camera);
        backend.ProfileGpuWait("Render/GpuWait/Output");
    }

    //结束渲染帧
    RenderOverlayPass();
    backend.ProfileGpuWait("Render/GpuWait/OverlayUI");
    backend.EndFrame();
    scene.EndRead();
    debugLines.clear();
    debugLineWorld = nullptr;
    FinishDrawSubmissions();
}

//清空本帧使用的显式提交并解除读取状态
void RenderSystem::FinishDrawSubmissions()
{
    activeInstanceSubmissions.clear();
    pendingSubmittedInstanceCount = 0;
    forwardPipeline.SetDrawSubmissions(nullptr);
    forwardPipeline.SetFrameWorld(nullptr);
    readingDrawSubmissions = false;
}

//验证一份显式实例提交是否可以使用
bool RenderSystem::ValidateInstanceSubmission(Mesh* mesh, Material* material, uint32 subMeshIndex,
    std::span<const MeshInstanceData> instances) const
{
    if (!mesh || !material || instances.empty()) return false;
    if (subMeshIndex >= mesh->subMeshes.size()) return false;

    const SubMesh& subMesh = mesh->subMeshes[subMeshIndex];
    usize start = static_cast<usize>(subMesh.indexStart);
    usize count = static_cast<usize>(subMesh.indexCount);
    if (count == 0 || start > mesh->indices.size() || count > mesh->indices.size() - start) return false;

    //显式实例提交拒绝多 Pass，不悄悄改变 Pass 顺序；几何契约已统一为 Standard，不再有兼容分支
    Shader* shader = material->shader.Get();
    return shader && shader->passes.size() == 1;
}

//接收一次显式实例提交
bool RenderSystem::SubmitInstances(World& world, const InstanceDrawOptions& options, Mesh* mesh, uint32 subMeshIndex,
    Material* material, std::span<const MeshInstanceData> instances, int32 sourceObjectId)
{
    //渲染读取期间、世界准备阶段与不支持实例化的后端都拒绝接收
    if (readingDrawSubmissions || !initialized || !window) return false;
    if (world.IsPreparing() || !backend.SupportsInstancing()) return false;
    //本帧预算按累计量判定，超出时整次提交失败，不接收前半段
    if (instances.size() > MaximumSubmittedInstances) return false;
    if (pendingSubmittedInstanceCount + instances.size() > MaximumSubmittedInstances) return false;
    if (!ValidateInstanceSubmission(mesh, material, subMeshIndex, instances)) return false;

    InstanceSubmission submission;
    submission.sourceObjectId = sourceObjectId;
    submission.world = &world;
    submission.contentRevision = world.GetContentRevision();
    submission.submissionId = nextInstanceSubmissionId++;
    submission.options = options;
    submission.mesh.Set(mesh);
    submission.material.Set(material);
    submission.subMeshIndex = subMeshIndex;
    submission.instances.assign(instances.begin(), instances.end());
    pendingSubmittedInstanceCount += submission.instances.size();
    pendingInstanceSubmissions.push_back(std::move(submission));
    return true;
}

void RenderSystem::OnWindowResize(int width, int height)
{
    //更新主帧缓冲尺寸
    framebufferWidth = width;
    framebufferHeight = height;
    //窗口逻辑尺寸跟着变，UI 的命中换算要用它。
    if (window)
    {
        uiRenderer.SetDisplayLogicalSize(vector2(static_cast<float32>(window->GetWidth()),
            static_cast<float32>(window->GetHeight())));
    }
}

RenderSystem::ManagedRenderTarget* RenderSystem::FindRenderTarget(RenderTargetID id)
{
    //查找离屏 RenderTarget
    for (ManagedRenderTarget& target : renderTargets)
    {
        if (target.id == id) return &target;
    }

    return nullptr;
}

const RenderSystem::ManagedRenderTarget* RenderSystem::FindRenderTarget(RenderTargetID id) const
{
    //查找只读离屏 RenderTarget
    for (const ManagedRenderTarget& target : renderTargets)
    {
        if (target.id == id) return &target;
    }

    return nullptr;
}

void RenderSystem::ReleaseRenderTargets()
{
    //释放离屏 RenderTarget
    for (ManagedRenderTarget& target : renderTargets)
    {
        backend.DeleteRenderTarget(target.renderTarget);
        backend.DeleteDepthTexture(target.depthTexture);
    }

    renderTargets.clear();
}

//释放所有相机颜色和深度快照资源。
void RenderSystem::ReleaseCameraFrameTextures()
{
    for (ManagedCameraFrameTextures& textures : cameraFrameTextures)
    {
        backend.DeleteRenderTarget(textures.sceneRenderTarget);
        backend.DeleteDepthTexture(textures.sceneDepthTexture);
        backend.DeleteRenderTarget(textures.renderTarget);
        backend.DeleteDepthTexture(textures.depthTexture);
    }

    cameraFrameTextures.clear();
}

//查找指定相机持有的颜色和深度快照资源。
RenderSystem::ManagedCameraFrameTextures* RenderSystem::FindCameraFrameTextures(EnsId cameraEns)
{
    for (ManagedCameraFrameTextures& textures : cameraFrameTextures)
    {
        if (textures.cameraEns == cameraEns) return &textures;
    }

    return nullptr;
}

void RenderSystem::PrepareCameraRenderData()
{
    for (ManagedCameraFrameTextures& textures : cameraFrameTextures)
    {
        textures.active = false;
    }

    for (RenderCamera& camera : scene.cameras)
    {
        camera.elapsedTime = elapsedTime;

        //下面任一分支提前退出时都不能留下悬空句柄，输出 Pass 会直接读 finalRenderTarget。
        camera.renderTarget = GpuRenderTargetID();
        camera.sceneColorTexture = GpuTextureID();
        camera.sceneDepthTexture = GpuDepthTextureID();
        camera.finalRenderTarget = GpuRenderTargetID();
        camera.cameraTextureTarget = GpuRenderTargetID();
        camera.cameraColorTexture = GpuTextureID();
        camera.cameraDepthTexture = GpuDepthTextureID();

        //解析相机声明的最终输出目标，0 表示默认窗口帧缓冲
        const ManagedRenderTarget* target = FindRenderTarget(camera.renderTargetId);
        const bool rendersToMainFramebuffer = !camera.renderTargetId.IsValid();
        if (!rendersToMainFramebuffer && !target)
        {
            camera.viewportWidth = 0;
            camera.viewportHeight = 0;
            continue;
        }

        //编辑态只渲染带离屏目标的相机，直接画主 framebuffer 的游戏相机不参与。
        //这条判定必须在分配内部缓冲之前，否则会为每个游戏相机白建一份场景缓冲。
        if (!mainFramebufferRendering && rendersToMainFramebuffer)
        {
            camera.viewportWidth = 0;
            camera.viewportHeight = 0;
            continue;
        }

        camera.finalRenderTarget = rendersToMainFramebuffer ? GpuRenderTargetID() : target->renderTarget;

        //视口仍然相对最终输出目标解析
        int32 targetWidth = target ? target->width : framebufferWidth;
        int32 targetHeight = target ? target->height : framebufferHeight;

        //计算相机视口
        CalculateViewportAxis(camera.normalizedViewportX, camera.normalizedViewportWidth, targetWidth, camera.viewportX, camera.viewportWidth);
        CalculateViewportAxis(camera.normalizedViewportY, camera.normalizedViewportHeight, targetHeight, camera.viewportY, camera.viewportHeight);
        if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) continue;

        //更新相机投影数据
        float32 aspect = static_cast<float32>(camera.viewportWidth) / static_cast<float32>(camera.viewportHeight);
        camera.projectionMatrix = RenderMath::Perspective(camera.fieldOfView, aspect, camera.nearPlane, camera.farPlane);
        camera.viewProjectionMatrix = RenderMath::Mul(camera.projectionMatrix, camera.viewMatrix);
        camera.viewFrustum = RenderMath::BuildFrustum(camera.viewProjectionMatrix);

        //曝光：项目级默认值，相机可单独覆盖
        camera.exposure = (camera.camera && camera.camera->overrideExposure)
            ? camera.camera->exposure
            : DisplaySettings::GetExposure();

        //准备相机纹理资源
        ManagedCameraFrameTextures* textures = FindCameraFrameTextures(camera.ens);
        if (!textures)
        {
            ManagedCameraFrameTextures created;
            created.cameraEns = camera.ens;
            cameraFrameTextures.push_back(created);
            textures = &cameraFrameTextures.back();
        }
        textures->active = true;

        if (textures->width != camera.viewportWidth || textures->height != camera.viewportHeight ||
            !textures->sceneRenderTarget.IsValid() || !textures->sceneDepthTexture.IsValid() ||
            !textures->depthTexture.IsValid() || !textures->renderTarget.IsValid())
        {
            GpuDepthTextureDesc depthDesc;
            depthDesc.width = camera.viewportWidth;
            depthDesc.height = camera.viewportHeight;
            depthDesc.floatingPoint = true;
            GpuDepthTextureID newSceneDepthTexture = backend.CreateDepthTexture(depthDesc);
            GpuDepthTextureID newDepthTexture = backend.CreateDepthTexture(depthDesc);

            //场景缓冲承载线性 HDR；折射快照必须同级格式，否则 blit 会把高光钳到 [0,1]
            GpuRenderTargetID newSceneRenderTarget;
            if (newSceneDepthTexture.IsValid())
            {
                GpuRenderTargetDesc sceneDesc;
                sceneDesc.width = camera.viewportWidth;
                sceneDesc.height = camera.viewportHeight;
                sceneDesc.depthTexture = newSceneDepthTexture;
                sceneDesc.format = GpuRenderTargetFormat::RGBA16F;
                newSceneRenderTarget = backend.CreateRenderTarget(sceneDesc);
            }

            GpuRenderTargetID newRenderTarget;
            if (newDepthTexture.IsValid())
            {
                GpuRenderTargetDesc targetDesc;
                targetDesc.width = camera.viewportWidth;
                targetDesc.height = camera.viewportHeight;
                targetDesc.depthTexture = newDepthTexture;
                targetDesc.linearColorFilter = true;
                targetDesc.format = GpuRenderTargetFormat::RGBA16F;
                newRenderTarget = backend.CreateRenderTarget(targetDesc);
            }

            if (newSceneRenderTarget.IsValid() && newRenderTarget.IsValid())
            {
                //替换相机纹理资源
                backend.DeleteRenderTarget(textures->sceneRenderTarget);
                backend.DeleteDepthTexture(textures->sceneDepthTexture);
                backend.DeleteRenderTarget(textures->renderTarget);
                backend.DeleteDepthTexture(textures->depthTexture);
                textures->sceneRenderTarget = newSceneRenderTarget;
                textures->sceneDepthTexture = newSceneDepthTexture;
                textures->sceneColorTexture = backend.GetRenderTargetColorTexture(newSceneRenderTarget);
                textures->renderTarget = newRenderTarget;
                textures->depthTexture = newDepthTexture;
                textures->width = camera.viewportWidth;
                textures->height = camera.viewportHeight;
            }
            else
            {
                backend.DeleteRenderTarget(newSceneRenderTarget);
                backend.DeleteDepthTexture(newSceneDepthTexture);
                backend.DeleteRenderTarget(newRenderTarget);
                backend.DeleteDepthTexture(newDepthTexture);
                Log::Error("RenderSystem camera texture setup failed: GPU resource creation failed.");
            }
        }

        //绑定相机纹理资源
        if (textures->width == camera.viewportWidth && textures->height == camera.viewportHeight &&
            textures->sceneRenderTarget.IsValid() && textures->sceneDepthTexture.IsValid() &&
            textures->depthTexture.IsValid() && textures->renderTarget.IsValid())
        {
            camera.renderTarget = textures->sceneRenderTarget;
            camera.sceneColorTexture = textures->sceneColorTexture;
            camera.sceneDepthTexture = textures->sceneDepthTexture;
            camera.cameraTextureTarget = textures->renderTarget;
            camera.cameraColorTexture = backend.GetRenderTargetColorTexture(textures->renderTarget);
            camera.cameraDepthTexture = textures->depthTexture;
        }
    }

    //释放已注销相机纹理
    auto textures = cameraFrameTextures.begin();
    while (textures != cameraFrameTextures.end())
    {
        if (textures->active)
        {
            ++textures;
            continue;
        }

        backend.DeleteRenderTarget(textures->sceneRenderTarget);
        backend.DeleteDepthTexture(textures->sceneDepthTexture);
        backend.DeleteRenderTarget(textures->renderTarget);
        backend.DeleteDepthTexture(textures->depthTexture);
        textures = cameraFrameTextures.erase(textures);
    }
}

void RenderSystem::RenderOverlayPass()
{
    //RetainedGUI 画在 ImGui 之前：编辑器界面永远盖在游戏 UI 上。
    //在游戏视图绘制屏幕画布
    if (!hasEditorPreviewTarget && mainFramebufferRendering)
    {
        UIOutputTarget uiTarget;
        uiTarget.width = framebufferWidth;
        uiTarget.height = framebufferHeight;
        uiRenderer.RenderOverlay(uiTarget);
    }

    if (!imguiLayer.IsInitialized()) return;

    //开始 GUI 覆盖层 Pass
    RenderPassDesc passDesc;
    passDesc.width = framebufferWidth;
    passDesc.height = framebufferHeight;
    passDesc.clearMode = ClearMode::None;
    backend.BeginPass(passDesc);

    imguiLayer.BeginFrame();

    //绘制运行时 GUI 和调试信息
    if (renderOverlay)
    {
        renderOverlay->DrawOverlay();
    }
    if (fpsLabelVisible)
    {
        imguiLayer.DrawFpsLabel();
    }

    //结束 GUI 覆盖层 Pass
    imguiLayer.Render();
    backend.EndPass();
}

//命中快照的深度回读入口；由 RetainedGuiFrame 的函数指针转进来。
bool RenderSystem::ReadUIDepth(uint64 viewId, uint64 viewerId, uint64 presentedFrame, int32 x, int32 y, float32* depth)
{
    if (!currentRenderSystem || !depth) return false;
    return currentRenderSystem->uiRenderer.ReadDepth(viewId, viewerId, presentedFrame, x, y, *depth);
}

/// <summary>获取当前渲染系统。</summary>
RenderSystem* RenderSystem::Current()
{
    return currentRenderSystem;
}

/// <summary>记录当前帧的世界空间调试线。</summary>
void RenderSystem::DrawLine(World& world, const vector3& start, const vector3& end, const color& tint,
    bool depthTest, uint32 drawLayer)
{
    if (!initialized) return;
    if (debugLineWorld != &world)
    {
        debugLines.clear();
        debugLineWorld = &world;
    }
    if (debugLines.size() >= 4096) return;
    //脚本传入的是显示色，写进线性场景缓冲前先转线性；之后会被输出 Pass 一起色调映射。
    debugLines.push_back({ start, end, ColorSpace::SrgbToLinear(tint), depthTest, drawLayer });
}

//取纹理的 GPU 句柄；资源管理器负责按需上传
GpuTextureID RenderSystem::GetTextureId(Texture2D* texture)
{
    if (!initialized || !texture) return GpuTextureID();
    return gpuResourceManager.GetTexture(texture);
}

/// <summary>获取离屏目标的深度纹理。</summary>
GpuDepthTextureID RenderSystem::GetRenderTargetDepthTexture(RenderTargetID id) const
{
    const ManagedRenderTarget* target = FindRenderTarget(id);
    return target ? target->depthTexture : GpuDepthTextureID();
}

//把完成相机绘制的线性场景缓冲转换到最终输出目标
void RenderSystem::RenderOutputPass(const RenderCamera& camera)
{
    if (!camera.sceneColorTexture.IsValid()) return;
    if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) return;

    OutputPass::Parameters parameters;
    parameters.sourceTexture = camera.sceneColorTexture;
    parameters.destinationRenderTarget = camera.finalRenderTarget;
    //目标是显示目标或默认帧缓冲，视口原点在这一层才有意义
    parameters.x = camera.viewportX;
    parameters.y = camera.viewportY;
    parameters.width = camera.viewportWidth;
    parameters.height = camera.viewportHeight;
    parameters.exposure = camera.exposure;
    outputPass.Render(parameters);
}
