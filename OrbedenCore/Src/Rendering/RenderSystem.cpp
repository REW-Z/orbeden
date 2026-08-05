#include "Rendering/RenderSystem.h"

#include "Log/Log.h"
#include "Rendering/RenderMath.h"
#include "Runtime/ResourceManager.h"

#include <algorithm>
#include <cmath>

namespace
{
    //将归一化轴参数转换为目标上的像素起点和长度。
    void CalculateViewportAxis(float32 normalizedStart, float32 normalizedSize, int32 targetSize, int32& start, int32& size)
    {
        //目标没有有效尺寸时，viewport 也必须被置为不可绘制。
        if (targetSize <= 0)
        {
            start = 0;
            size = 0;
            return;
        }

        //先限制起点，避免浮点配置将 viewport 推到目标范围之外。
        start = std::clamp(static_cast<int32>(normalizedStart * static_cast<float32>(targetSize)), 0, targetSize - 1);
        if (normalizedSize <= 0.0f)
        {
            size = 0;
            return;
        }

        //结束点至少覆盖一个像素，并将最终范围限制在目标尺寸内。
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
    //重复初始化视为成功，保持系统的幂等行为。
    if (initialized) return true;

    //渲染后端必须绑定有效窗口才能创建 framebuffer 资源。
    if (!renderWindow)
    {
        Log::Error("RenderSystem initialize failed: window is missing.");
        return false;
    }

    //记录主 framebuffer 尺寸，后续用于默认相机目标和 viewport 解析。
    window = renderWindow;
    framebufferWidth = window->GetFramebufferWidth();
    framebufferHeight = window->GetFramebufferHeight();

    //先初始化后端，再让资源管理器和 Forward 管线引用后端。
    if (!backend.Initialize(window))
    {
        Log::Error("RenderSystem initialize failed: backend initialize failed.");
        return false;
    }

    gpuResourceManager.Initialize(&backend);
    forwardPipeline.Initialize(&backend);
    elapsedTime = 0.0f;

    //ImGui 是可选覆盖层，初始化失败不影响场景渲染。
    if (!imguiLayer.Initialize(window))
    {
        Log::Warning("RenderSystem initialize warning: ImGui overlay is disabled.");
    }

    initialized = true;
    return true;
}

void RenderSystem::Shutdown()
{
    //按照使用依赖的逆序释放 UI、离屏目标、管线、缓存和后端。
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
    //未初始化或尺寸无效时，不向后端创建半成品资源。
    if (!initialized || width <= 0 || height <= 0) return RenderTargetID();

    //先创建深度纹理，离屏目标创建时将其作为深度附件绑定。
    GpuDepthTextureDesc depthDesc;
    depthDesc.width = width;
    depthDesc.height = height;
    GpuDepthTextureID depthTexture = backend.CreateDepthTexture(depthDesc);
    if (!depthTexture.IsValid()) return RenderTargetID();

    //创建颜色目标；失败时回收已经创建的深度纹理。
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

    //生成不与现有目标冲突的逻辑 ID，0 保留为无效 ID。
    RenderTargetID id;
    do
    {
        id.id = nextRenderTargetId++;
        if (nextRenderTargetId == 0) nextRenderTargetId = 1;
    } while (!id.IsValid() || FindRenderTarget(id));

    //保存逻辑 ID 与后端句柄的对应关系，供相机和外部系统查询。
    renderTargets.push_back({ id, depthTexture, renderTarget, width, height });
    return id;
}

bool RenderSystem::ResizeRenderTarget(RenderTargetID id, int32 width, int32 height)
{
    //目标不存在或尺寸无效时保持原资源不变。
    ManagedRenderTarget* target = FindRenderTarget(id);
    if (!target || width <= 0 || height <= 0) return false;
    if (target->width == width && target->height == height) return true;

    //先创建新尺寸资源，确保创建失败时旧目标仍可继续使用。
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

    //新资源创建成功后，再释放旧资源并原子地替换记录。
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
    //先找到逻辑目标；未知 ID 直接忽略，保持删除接口幂等。
    auto it = std::find_if(renderTargets.begin(), renderTargets.end(), [id](const ManagedRenderTarget& target)
    {
        return target.id == id;
    });
    if (it == renderTargets.end()) return;

    //释放后端资源，再移除逻辑映射。
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

    //释放依赖旧 CPU 资源的 GPU 状态，后端和离屏目标继续保持有效。
    forwardPipeline.InvalidateResourceCaches();
    gpuResourceManager.InvalidateCaches();
    warnedMissingCamera = false;
}

void RenderSystem::Render(World& world, float deltaTime)
{
    if (!initialized || !window) return;

    //仅累加有效的正时间步，避免暂停或异常时间污染 Shader 时间。
    if (std::isfinite(deltaTime) && deltaTime > 0.0f)
    {
        elapsedTime += deltaTime;
    }

    //在新一帧读取场景前释放已销毁对象对应的 GPU 资源。
    gpuResourceManager.ReleaseDestroyedResources();

    //增量刷新持久场景，并在读取阶段延迟组件结构变化
    scene.Update(world, transformCache);

    //根据离屏目标尺寸准备相机 viewport，并重建依赖 viewport 的投影数据。
    PrepareCameraRenderData();

    scene.BeginRead();

    //所有相机共享同一帧的后端 frame 生命周期。
    backend.BeginFrame();


    //无相机  
    if (scene.cameras.empty())
    {
        //没有相机时只提交一次黑色清屏 pass，避免留下未定义的 framebuffer 内容。
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

        //即使没有场景相机，也允许调试覆盖层显示诊断信息。
        RenderDebugOverlay();
        backend.EndFrame();
        scene.EndRead();
        return;
    }

    //阴影资源只准备一次，然后由所有相机共享。
    warnedMissingCamera = false;
    forwardPipeline.PrepareFrame(scene, gpuResourceManager);

    //逐相机执行剔除、排序和 Forward 绘制；无效 viewport 的相机跳过。
    for (const RenderCamera& camera : scene.cameras)
    {
        if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) continue;

        culler.Cull(scene, camera, visibleSet); //剔除
        scene.BuildRenderItems(visibleSet);
        sorter.Sort(visibleSet);//排序
        forwardPipeline.Render(scene, visibleSet, gpuResourceManager);//forword绘制
    }

    //场景 pass 完成后绘制覆盖层，并结束本帧后端命令提交。
    RenderDebugOverlay();
    backend.EndFrame();
    scene.EndRead();
}

void RenderSystem::OnWindowResize(int width, int height)
{
    //只更新主 framebuffer 尺寸，具体相机 viewport 在下一帧统一重新解析。
    framebufferWidth = width;
    framebufferHeight = height;
}

RenderSystem::ManagedRenderTarget* RenderSystem::FindRenderTarget(RenderTargetID id)
{
    //离屏目标数量通常较少，线性查找可以保持实现简单且避免额外索引。
    for (ManagedRenderTarget& target : renderTargets)
    {
        if (target.id == id) return &target;
    }

    return nullptr;
}

const RenderSystem::ManagedRenderTarget* RenderSystem::FindRenderTarget(RenderTargetID id) const
{
    //const 重载与非 const 版本保持相同的逻辑 ID 查找规则。
    for (const ManagedRenderTarget& target : renderTargets)
    {
        if (target.id == id) return &target;
    }

    return nullptr;
}

void RenderSystem::ReleaseRenderTargets()
{
    //先逐个释放后端资源，再清空逻辑记录。
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

        //解析相机指定的离屏目标；目标已被删除时使该相机本帧不可绘制。
        const ManagedRenderTarget* target = FindRenderTarget(camera.renderTargetId);
        if (camera.renderTargetId.IsValid() && !target)
        {
            camera.renderTarget = GpuRenderTargetID();
            camera.viewportWidth = 0;
            camera.viewportHeight = 0;
            continue;
        }

        //没有离屏目标时使用主 framebuffer，否则使用目标自身尺寸。
        int32 targetWidth = target ? target->width : framebufferWidth;
        int32 targetHeight = target ? target->height : framebufferHeight;
        camera.renderTarget = target ? target->renderTarget : GpuRenderTargetID();

        //将归一化 viewport 转为像素范围；空范围的相机会被主渲染循环跳过。
        CalculateViewportAxis(camera.normalizedViewportX, camera.normalizedViewportWidth, targetWidth, camera.viewportX, camera.viewportWidth);
        CalculateViewportAxis(camera.normalizedViewportY, camera.normalizedViewportHeight, targetHeight, camera.viewportY, camera.viewportHeight);
        if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) continue;

        //viewport 宽高比变化会影响投影矩阵、视锥和后续可见性剔除结果。
        float32 aspect = static_cast<float32>(camera.viewportWidth) / static_cast<float32>(camera.viewportHeight);
        camera.projectionMatrix = RenderMath::Perspective(camera.fieldOfView, aspect, camera.nearPlane, camera.farPlane);
        camera.viewProjectionMatrix = RenderMath::Mul(camera.projectionMatrix, camera.viewMatrix);
        camera.viewFrustum = RenderMath::BuildFrustum(camera.viewProjectionMatrix);

        //按相机 viewport 创建或重建独立的颜色和深度快照。
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
                //新资源完整创建后再替换旧快照，避免尺寸变化时留下半成品。
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

        //只有尺寸与当前 viewport 一致的完整资源才能交给 Refraction Pass。
        if (textures->width == camera.viewportWidth && textures->height == camera.viewportHeight &&
            textures->depthTexture.IsValid() && textures->renderTarget.IsValid())
        {
            camera.cameraTextureTarget = textures->renderTarget;
            camera.cameraColorTexture = backend.GetRenderTargetColorTexture(textures->renderTarget);
            camera.cameraDepthTexture = textures->depthTexture;
        }
    }

    //相机注销后立即释放其快照，避免编辑器频繁创建相机时累积 GPU 资源。
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

void RenderSystem::RenderDebugOverlay()
{
    if (!imguiLayer.IsInitialized()) return;

    //覆盖层不清除场景内容，只在主 framebuffer 上追加 UI 绘制。
    RenderPassDesc passDesc;
    passDesc.width = framebufferWidth;
    passDesc.height = framebufferHeight;
    passDesc.clearMode = ClearMode::None;
    backend.BeginPass(passDesc);

    imguiLayer.BeginFrame();

    //先绘制外部覆盖层，再绘制渲染系统自带的 FPS 标签。
    if (renderOverlay)
    {
        renderOverlay->DrawOverlay();
    }
    if (fpsLabelVisible)
    {
        imguiLayer.DrawFpsLabel();
    }

    //提交 ImGui 命令并结束覆盖层 pass。
    imguiLayer.Render();
    backend.EndPass();
}
