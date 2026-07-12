#include "Rendering/RenderSystem.h"

#include "Log/Log.h"
#include "Rendering/RenderMath.h"

#include <algorithm>

namespace
{
    void CalculateViewportAxis(float32 normalizedStart, float32 normalizedSize, int32 targetSize, int32& start, int32& size)
    {
        if (targetSize <= 0)
        {
            start = 0;
            size = 0;
            return;
        }

        start = std::clamp(static_cast<int32>(normalizedStart * static_cast<float32>(targetSize)), 0, targetSize - 1);
        if (normalizedSize <= 0.0f)
        {
            size = 0;
            return;
        }

        int32 end = std::clamp(static_cast<int32>((normalizedStart + normalizedSize) * static_cast<float32>(targetSize)), start + 1, targetSize);
        size = end - start;
    }
}

bool RenderSystem::Initialize(IWindow* renderWindow)
{
    if (initialized) return true;
    if (!renderWindow)
    {
        Log::Error("RenderSystem initialize failed: window is missing.");
        return false;
    }

    window = renderWindow;
    framebufferWidth = window->GetFramebufferWidth();
    framebufferHeight = window->GetFramebufferHeight();

    if (!backend.Initialize(window))
    {
        Log::Error("RenderSystem initialize failed: backend initialize failed.");
        return false;
    }

    resources.Initialize(&backend);
    forwardPipeline.Initialize(&backend);
    if (!imguiLayer.Initialize(window))
    {
        Log::Warning("RenderSystem initialize warning: ImGui overlay is disabled.");
    }

    initialized = true;
    return true;
}

void RenderSystem::Shutdown()
{
    renderOverlay = nullptr;
    imguiLayer.Shutdown();
    ReleaseRenderTargets();
    forwardPipeline.Shutdown();
    resources.Shutdown();
    backend.Shutdown();
    initialized = false;
    window = nullptr;
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
    if (!initialized || width <= 0 || height <= 0) return RenderTargetID();

    GpuDepthTextureDesc depthDesc;
    depthDesc.width = width;
    depthDesc.height = height;
    GpuDepthTextureID depthTexture = backend.CreateDepthTexture(depthDesc);
    if (!depthTexture.IsValid()) return RenderTargetID();

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

    RenderTargetID id;
    do
    {
        id.id = nextRenderTargetId++;
        if (nextRenderTargetId == 0) nextRenderTargetId = 1;
    } while (!id.IsValid() || FindRenderTarget(id));

    renderTargets.push_back({ id, depthTexture, renderTarget, width, height });
    return id;
}

bool RenderSystem::ResizeRenderTarget(RenderTargetID id, int32 width, int32 height)
{
    ManagedRenderTarget* target = FindRenderTarget(id);
    if (!target || width <= 0 || height <= 0) return false;
    if (target->width == width && target->height == height) return true;

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
    auto it = std::find_if(renderTargets.begin(), renderTargets.end(), [id](const ManagedRenderTarget& target)
    {
        return target.id == id;
    });
    if (it == renderTargets.end()) return;

    backend.DeleteRenderTarget(it->renderTarget);
    backend.DeleteDepthTexture(it->depthTexture);
    renderTargets.erase(it);
}

GpuTextureID RenderSystem::GetRenderTargetTexture(RenderTargetID id) const
{
    const ManagedRenderTarget* target = FindRenderTarget(id);
    return target ? backend.GetRenderTargetColorTexture(target->renderTarget) : GpuTextureID();
}

//获取仅供当前帧覆盖层只读访问的渲染场景
const RenderScene& RenderSystem::GetCurrentScene() const
{
    return scene;
}

void RenderSystem::PrepareProjectReload()
{
    if (!initialized) return;

    ReleaseRenderTargets();
    forwardPipeline.Shutdown();
    resources.Shutdown();
    warnedMissingCamera = false;
}

void RenderSystem::CompleteProjectReload()
{
    if (!initialized) return;

    resources.Initialize(&backend);
    forwardPipeline.Initialize(&backend);
    warnedMissingCamera = false;
}

void RenderSystem::Render(World& world, float deltaTime)
{
    (void)deltaTime;
    if (!initialized || !window) return;

    resources.CollectUnused();
    sceneBuilder.Build(world, spaceCache, framebufferWidth, framebufferHeight, scene);
    ResolveCameraTargets();
    backend.BeginFrame();
    if (scene.cameras.empty())
    {
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
        RenderDebugOverlay();
        backend.EndFrame();
        return;
    }

    warnedMissingCamera = false;
    forwardPipeline.PrepareFrame(scene, resources);
    for (const RenderCamera& camera : scene.cameras)
    {
        if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) continue;
        culler.Cull(scene, camera, visibleSet);
        sorter.Sort(scene, visibleSet);
        forwardPipeline.Render(scene, visibleSet, resources);
    }
    RenderDebugOverlay();
    backend.EndFrame();
}

void RenderSystem::OnWindowResize(int width, int height)
{
    framebufferWidth = width;
    framebufferHeight = height;
}

RenderSystem::ManagedRenderTarget* RenderSystem::FindRenderTarget(RenderTargetID id)
{
    for (ManagedRenderTarget& target : renderTargets)
    {
        if (target.id == id) return &target;
    }

    return nullptr;
}

const RenderSystem::ManagedRenderTarget* RenderSystem::FindRenderTarget(RenderTargetID id) const
{
    for (const ManagedRenderTarget& target : renderTargets)
    {
        if (target.id == id) return &target;
    }

    return nullptr;
}

void RenderSystem::ReleaseRenderTargets()
{
    for (ManagedRenderTarget& target : renderTargets)
    {
        backend.DeleteRenderTarget(target.renderTarget);
        backend.DeleteDepthTexture(target.depthTexture);
    }

    renderTargets.clear();
}

void RenderSystem::ResolveCameraTargets()
{
    for (RenderCamera& camera : scene.cameras)
    {
        const ManagedRenderTarget* target = FindRenderTarget(camera.renderTargetId);
        if (camera.renderTargetId.IsValid() && !target)
        {
            camera.renderTarget = GpuRenderTargetID();
            camera.viewportWidth = 0;
            camera.viewportHeight = 0;
            continue;
        }

        int32 targetWidth = target ? target->width : framebufferWidth;
        int32 targetHeight = target ? target->height : framebufferHeight;
        camera.renderTarget = target ? target->renderTarget : GpuRenderTargetID();

        CalculateViewportAxis(camera.normalizedViewportX, camera.normalizedViewportWidth, targetWidth, camera.viewportX, camera.viewportWidth);
        CalculateViewportAxis(camera.normalizedViewportY, camera.normalizedViewportHeight, targetHeight, camera.viewportY, camera.viewportHeight);
        if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) continue;

        float32 aspect = static_cast<float32>(camera.viewportWidth) / static_cast<float32>(camera.viewportHeight);
        camera.projectionMatrix = RenderMath::Perspective(camera.fieldOfView, aspect, camera.nearPlane, camera.farPlane);
        camera.viewProjectionMatrix = RenderMath::Mul(camera.projectionMatrix, camera.viewMatrix);
        camera.viewFrustum = RenderMath::BuildFrustum(camera.viewProjectionMatrix);
    }
}

void RenderSystem::RenderDebugOverlay()
{
    if (!imguiLayer.IsInitialized()) return;

    RenderPassDesc passDesc;
    passDesc.width = framebufferWidth;
    passDesc.height = framebufferHeight;
    passDesc.clearMode = ClearMode::None;
    backend.BeginPass(passDesc);

    imguiLayer.BeginFrame();
    if (renderOverlay)
    {
        renderOverlay->DrawOverlay();
    }
    if (fpsLabelVisible)
    {
        imguiLayer.DrawFpsLabel();
    }
    imguiLayer.Render();
    backend.EndPass();
}
