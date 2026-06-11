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
    initialized = true;
    return true;
}

void RenderSystem::Shutdown()
{
    forwardPipeline.Shutdown();
    resources.Shutdown();
    backend.Shutdown();
    initialized = false;
    window = nullptr;
}

void RenderSystem::Render(World& world, float deltaTime)
{
    (void)deltaTime;
    if (!initialized || !window) return;

    sceneBuilder.Build(world, spaceCache, framebufferWidth, framebufferHeight, scene);
    if (scene.cameras.empty())
    {
        if (!warnedMissingCamera)
        {
            Log::Warning("RenderSystem render skipped: scene has no Camera.");
            warnedMissingCamera = true;
        }
        return;
    }

    warnedMissingCamera = false;
    backend.BeginFrame();
    for (const RenderCamera& camera : scene.cameras)
    {
        culler.Cull(scene, camera, visibleSet);
        sorter.Sort(visibleSet);
        forwardPipeline.Render(scene, visibleSet, resources);
    }
    backend.EndFrame();
}

void RenderSystem::OnWindowResize(int width, int height)
{
    framebufferWidth = width;
    framebufferHeight = height;
}
