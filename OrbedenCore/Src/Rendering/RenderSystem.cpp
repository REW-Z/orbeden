#include "Rendering/RenderSystem.h"

#include "Log/Log.h"
#include "Rendering/RenderMath.h"

#include <algorithm>

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

    resources.Initialize(&backend);
    forwardPipeline.Initialize(&backend);

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
    //覆盖层由外部持有，关闭时只解除引用，不负责删除对象。
    renderOverlay = nullptr;

    //按照使用依赖的逆序释放 UI、离屏目标、管线、缓存和后端。
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

//获取仅供当前帧覆盖层只读访问的渲染场景
const RenderScene& RenderSystem::GetCurrentScene() const
{
    return scene;
}

void RenderSystem::PrepareProjectReload()
{
    if (!initialized) return;

    //项目切换前释放依赖旧资源的离屏目标、管线资源和上传缓存。
    ReleaseRenderTargets();
    forwardPipeline.Shutdown();
    resources.Shutdown();
    warnedMissingCamera = false;
}

void RenderSystem::CompleteProjectReload()
{
    if (!initialized) return;

    //项目切换完成后重新绑定当前后端，延迟到下一帧按需上传项目资源。
    resources.Initialize(&backend);
    forwardPipeline.Initialize(&backend);
    warnedMissingCamera = false;
}

void RenderSystem::Render(World& world, float deltaTime)
{
    (void)deltaTime;
    if (!initialized || !window) return;

    //回收已经销毁的 CPU 资源，并从世界构建当前帧的渲染快照。
    resources.CollectUnused();
    sceneBuilder.Build(world, spaceCache, framebufferWidth, framebufferHeight, scene);

    //根据离屏目标尺寸解析相机 viewport，并重建依赖 viewport 的投影数据。
    ResolveCameraTargets();

    //所有相机共享同一帧的后端 frame 生命周期。
    backend.BeginFrame();
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
        return;
    }

    //阴影资源只准备一次，然后由所有相机共享。
    warnedMissingCamera = false;
    forwardPipeline.PrepareFrame(scene, resources);

    //逐相机执行剔除、排序和 Forward 绘制；无效 viewport 的相机跳过。
    for (const RenderCamera& camera : scene.cameras)
    {
        if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) continue;

        culler.Cull(scene, camera, visibleSet);
        sorter.Sort(scene, visibleSet);
        forwardPipeline.Render(scene, visibleSet, resources);
    }

    //场景 pass 完成后绘制覆盖层，并结束本帧后端命令提交。
    RenderDebugOverlay();
    backend.EndFrame();
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

void RenderSystem::ResolveCameraTargets()
{
    for (RenderCamera& camera : scene.cameras)
    {
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
