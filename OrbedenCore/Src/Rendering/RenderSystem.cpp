#include "Rendering/RenderSystem.h"

#include "Log/Log.h"
#include "Rendering/RenderMath.h"
#include "Runtime/ResourceManager.h"

#include <algorithm>
#include <cmath>

namespace
{
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
    elapsedTime = 0.0f;

    //初始化 ImGui 覆盖层
    if (!imguiLayer.Initialize(window))
    {
        Log::Warning("RenderSystem initialize warning: ImGui overlay is disabled.");
    }

    initialized = true;
    return true;
}

void RenderSystem::Shutdown()
{
    //释放渲染系统资源
    imguiLayer.Shutdown();
    ReleaseCameraFrameTextures();
    ReleaseRenderTargets();
    forwardPipeline.Shutdown();
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

void RenderSystem::SetFpsLabelVisible(bool value)
{
    fpsLabelVisible = value;
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
    warnedMissingCamera = false;
}

void RenderSystem::Render(World& world, float deltaTime)
{
    if (!initialized || !window) return;

    //累加 Shader 时间
    if (std::isfinite(deltaTime) && deltaTime > 0.0f)
    {
        elapsedTime += deltaTime;
    }

    //释放已销毁对象的 GPU 资源
    gpuResourceManager.ReleaseDestroyedResources();

    //刷新持久渲染场景
    scene.Update(world, transformCache);

    //准备相机渲染数据
    PrepareCameraRenderData();

    scene.BeginRead();

    //开始渲染帧
    backend.BeginFrame();


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

        //绘制无相机场景的覆盖层
        RenderOverlayPass();
        backend.EndFrame();
        scene.EndRead();
        return;
    }

    //准备共享阴影资源
    warnedMissingCamera = false;
    forwardPipeline.PrepareFrame(scene, gpuResourceManager);

    //绘制活动相机
    for (const RenderCamera& camera : scene.cameras)
    {
        if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) continue;

        culler.Cull(scene, camera, visibleSet); //剔除
        scene.BuildRenderItems(visibleSet);
        sorter.Sort(visibleSet);//排序
        forwardPipeline.Render(scene, visibleSet, gpuResourceManager);//forword绘制
    }

    //结束渲染帧
    RenderOverlayPass();
    backend.EndFrame();
    scene.EndRead();
}

void RenderSystem::OnWindowResize(int width, int height)
{
    //更新主帧缓冲尺寸
    framebufferWidth = width;
    framebufferHeight = height;
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

        //解析相机离屏 RenderTarget
        const ManagedRenderTarget* target = FindRenderTarget(camera.renderTargetId);
        if (camera.renderTargetId.IsValid() && !target)
        {
            camera.renderTarget = GpuRenderTargetID();
            camera.viewportWidth = 0;
            camera.viewportHeight = 0;
            continue;
        }

        //获取相机渲染目标尺寸
        int32 targetWidth = target ? target->width : framebufferWidth;
        int32 targetHeight = target ? target->height : framebufferHeight;
        camera.renderTarget = target ? target->renderTarget : GpuRenderTargetID();

        //计算相机视口
        CalculateViewportAxis(camera.normalizedViewportX, camera.normalizedViewportWidth, targetWidth, camera.viewportX, camera.viewportWidth);
        CalculateViewportAxis(camera.normalizedViewportY, camera.normalizedViewportHeight, targetHeight, camera.viewportY, camera.viewportHeight);
        if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) continue;

        //更新相机投影数据
        float32 aspect = static_cast<float32>(camera.viewportWidth) / static_cast<float32>(camera.viewportHeight);
        camera.projectionMatrix = RenderMath::Perspective(camera.fieldOfView, aspect, camera.nearPlane, camera.farPlane);
        camera.viewProjectionMatrix = RenderMath::Mul(camera.projectionMatrix, camera.viewMatrix);
        camera.viewFrustum = RenderMath::BuildFrustum(camera.viewProjectionMatrix);

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
            !textures->depthTexture.IsValid() || !textures->renderTarget.IsValid())
        {
            GpuDepthTextureDesc depthDesc;
            depthDesc.width = camera.viewportWidth;
            depthDesc.height = camera.viewportHeight;
            GpuDepthTextureID newDepthTexture = backend.CreateDepthTexture(depthDesc);

            GpuRenderTargetID newRenderTarget;
            if (newDepthTexture.IsValid())
            {
                GpuRenderTargetDesc targetDesc;
                targetDesc.width = camera.viewportWidth;
                targetDesc.height = camera.viewportHeight;
                targetDesc.depthTexture = newDepthTexture;
                targetDesc.linearColorFilter = true;
                newRenderTarget = backend.CreateRenderTarget(targetDesc);
            }

            if (newDepthTexture.IsValid() && newRenderTarget.IsValid())
            {
                //替换相机纹理资源
                backend.DeleteRenderTarget(textures->renderTarget);
                backend.DeleteDepthTexture(textures->depthTexture);
                textures->depthTexture = newDepthTexture;
                textures->renderTarget = newRenderTarget;
                textures->width = camera.viewportWidth;
                textures->height = camera.viewportHeight;
            }
            else
            {
                backend.DeleteRenderTarget(newRenderTarget);
                backend.DeleteDepthTexture(newDepthTexture);
                Log::Error("RenderSystem camera texture setup failed: GPU resource creation failed.");
            }
        }

        //绑定相机纹理资源
        if (textures->width == camera.viewportWidth && textures->height == camera.viewportHeight &&
            textures->depthTexture.IsValid() && textures->renderTarget.IsValid())
        {
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

        backend.DeleteRenderTarget(textures->renderTarget);
        backend.DeleteDepthTexture(textures->depthTexture);
        textures = cameraFrameTextures.erase(textures);
    }
}

void RenderSystem::RenderOverlayPass()
{
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
