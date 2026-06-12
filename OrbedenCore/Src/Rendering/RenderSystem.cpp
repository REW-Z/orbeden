#include "Rendering/RenderSystem.h"

#include "Log/Log.h"

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
    editorOverlay = nullptr;
    imguiLayer.Shutdown();
    forwardPipeline.Shutdown();
    resources.Shutdown();
    backend.Shutdown();
    initialized = false;
    window = nullptr;
}

void RenderSystem::SetEditorOverlay(IImGuiOverlay* overlay)
{
    editorOverlay = overlay;
}

void RenderSystem::PrepareProjectReload()
{
    if (!initialized) return;

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

    sceneBuilder.Build(world, spaceCache, framebufferWidth, framebufferHeight, scene);
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
    for (const RenderCamera& camera : scene.cameras)
    {
        culler.Cull(scene, camera, visibleSet);
        sorter.Sort(visibleSet);
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

void RenderSystem::RenderDebugOverlay()
{
    imguiLayer.BeginFrame();
    if (editorOverlay)
    {
        editorOverlay->DrawImGui();
    }
    imguiLayer.DrawFpsLabel();
    imguiLayer.Render();
}
