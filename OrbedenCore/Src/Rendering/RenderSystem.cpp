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

    //选择描边遮罩：把选中几何按描边色平涂进遮罩，深度测试照常进行以保留遮挡关系。
    //裁剪坐标必须和材质着色器用同一套算式，深度才能逐位相等，否则遮罩会出现深度抖动。
    constexpr const char* OutlineMaskVertexSource = R"(#version 430 core
layout(location = 0) in vec3 a_Position;
uniform mat4 u_Model;
uniform mat4 u_ViewProjection;
uniform vec4 u_Tint;
out vec4 v_Tint;
void main()
{
    v_Tint = u_Tint;
    gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
}
)";

    constexpr const char* OutlineMaskFragmentSource = R"(#version 430 core
in vec4 v_Tint;
out vec4 FragColor;
void main()
{
    FragColor = v_Tint;
}
)";

    //选择描边合成：全屏采样遮罩，只在轮廓外侧压色，物体自身保持原色
    constexpr const char* OutlineCompositeVertexSource = R"(#version 430 core
layout(location = 0) in vec3 a_Position;
out vec2 v_Uv;
void main()
{
    v_Uv = a_Position.xy * 0.5 + 0.5;
    gl_Position = vec4(a_Position.xy, 0.0, 1.0);
}
)";

    constexpr const char* OutlineCompositeFragmentSource = R"(#version 430 core
in vec2 v_Uv;
out vec4 FragColor;
uniform sampler2D u_Mask;
uniform vec3 u_TexelSize;
uniform float u_CoreRadius;
uniform float u_GlowRadius;
uniform float u_GlowAlpha;

//沿固定方向在指定半径的圆周上取覆盖，得到这一圈里最强的描边色
vec4 SampleRing(float radius)
{
    vec4 result = vec4(0.0);
    for (int index = 0; index < 16; ++index)
    {
        float angle = 6.28318531 * float(index) / 16.0;
        vec2 offset = vec2(cos(angle), sin(angle)) * radius * u_TexelSize.xy;
        vec4 sampled = texture(u_Mask, v_Uv + offset);
        if (sampled.a > result.a) result = sampled;
    }
    return result;
}

void main()
{
    //物体自身保持原色，只有外侧才压描边
    if (texture(u_Mask, v_Uv).a > 0.0) discard;

    vec4 core = SampleRing(u_CoreRadius);
    vec4 glow = SampleRing(u_GlowRadius);
    float coreAlpha = core.a;
    float glowAlpha = glow.a * u_GlowAlpha;
    float alpha = max(coreAlpha, glowAlpha);
    if (alpha <= 0.001) discard;

    vec3 tint = coreAlpha >= glowAlpha ? core.rgb : glow.rgb;
    FragColor = vec4(tint, alpha);
}
)";

    //描边合成结果的混合方式：按覆盖度叠加，未覆盖像素不写入
    constexpr float32 OutlineCoreRadius = 2.0f;
    constexpr float32 OutlineGlowRadius = 7.0f;
    constexpr float32 OutlineGlowAlpha = 0.35f;
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
    outputPass.Initialize(&backend);
    outlineQuad.Initialize(&backend);
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
    ReleaseOutlineResources();
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
    warnedMissingCamera = false;
}

void RenderSystem::Render(World& world, float deltaTime)
{
    PROFILE("Render/Frame");

    if (!initialized || !window) return;

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
        debugLines.clear();
        debugLineWorld = nullptr;
        return;
    }

    //准备共享阴影资源
    warnedMissingCamera = false;
    forwardPipeline.PrepareFrame(scene, gpuResourceManager);

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

        RenderSelectionOutline(camera, visibleSet);//选择描边后处理
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

        //输出 Pass 必须排在描边与调试线之后：那些内容也写进场景缓冲，要一起转换到显示空间
        RenderOutputPass(camera);
    }

    //结束渲染帧
    RenderOverlayPass();
    backend.EndFrame();
    scene.EndRead();
    debugLines.clear();
    debugLineWorld = nullptr;
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

/// <summary>提交本帧需要描边的物体。</summary>
void RenderSystem::SetSelectionHighlights(const List<SelectionHighlight>& highlights)
{
    //描边色同样来自编辑器界面，语义是显示色
    selectionHighlights = highlights;
    for (SelectionHighlight& highlight : selectionHighlights)
    {
        highlight.tint = ColorSpace::SrgbToLinear(highlight.tint);
    }
}

/// <summary>获取离屏目标的深度纹理。</summary>
GpuDepthTextureID RenderSystem::GetRenderTargetDepthTexture(RenderTargetID id) const
{
    const ManagedRenderTarget* target = FindRenderTarget(id);
    return target ? target->depthTexture : GpuDepthTextureID();
}

//查找本帧提交的描边颜色
color RenderSystem::FindHighlightTint(EnsId ens) const
{
    for (const SelectionHighlight& highlight : selectionHighlights)
    {
        if (highlight.ens == ens) return highlight.tint;
    }
    return color { 0.0f, 0.0f, 0.0f, 0.0f };
}

//准备选择描边所需的内置 shader 和全屏四边形
bool RenderSystem::PrepareOutlineResources()
{
    if (!outlineMaskProgram.IsValid())
    {
        GpuShaderProgramDesc maskDesc;
        maskDesc.vertexSource = OutlineMaskVertexSource;
        maskDesc.fragmentSource = OutlineMaskFragmentSource;
        outlineMaskProgram = backend.CreateShaderProgram(maskDesc);
    }

    if (!outlineCompositeProgram.IsValid())
    {
        GpuShaderProgramDesc compositeDesc;
        compositeDesc.vertexSource = OutlineCompositeVertexSource;
        compositeDesc.fragmentSource = OutlineCompositeFragmentSource;
        outlineCompositeProgram = backend.CreateShaderProgram(compositeDesc);
    }

    //全屏四边形与输出 Pass 共用同一份实现
    const bool ready = outlineMaskProgram.IsValid() && outlineCompositeProgram.IsValid() && outlineQuad.EnsureReady();
    if (!ready && !outlineWarned)
    {
        Log::Error("RenderSystem selection outline setup failed: builtin outline resources are unavailable.");
        outlineWarned = true;
    }
    return ready;
}

//释放选择描边持有的后端资源
void RenderSystem::ReleaseOutlineResources()
{
    outlineQuad.Shutdown();
    if (outlineMaskTarget.IsValid()) backend.DeleteRenderTarget(outlineMaskTarget);
    if (outlineMaskProgram.IsValid()) backend.DeleteShaderProgram(outlineMaskProgram);
    if (outlineCompositeProgram.IsValid()) backend.DeleteShaderProgram(outlineCompositeProgram);

    outlineMaskTarget = GpuRenderTargetID();
    outlineMaskTexture = GpuTextureID();
    outlineMaskProgram = GpuShaderProgramID();
    outlineCompositeProgram = GpuShaderProgramID();
    outlineMaskWidth = 0;
    outlineMaskHeight = 0;
    outlineWarned = false;
}

//为指定相机绘制选择描边：先写遮罩，再合成到相机颜色目标
void RenderSystem::RenderSelectionOutline(const RenderCamera& camera, const VisibleSet& visibleSet)
{
    if (selectionHighlights.empty()) return;

    const int32 viewportWidth = camera.viewportWidth;
    const int32 viewportHeight = camera.viewportHeight;
    if (viewportWidth <= 0 || viewportHeight <= 0) return;
    if (!PrepareOutlineResources()) return;

    //遮罩目标与相机视口同尺寸，并共享相机场景缓冲的深度纹理：描边因此会被前景遮挡。
    //深度必须取场景缓冲的，renderTargetId 现在指向显示目标，那份深度从未被写入。
    if (!outlineMaskTarget.IsValid() || outlineMaskWidth != viewportWidth || outlineMaskHeight != viewportHeight)
    {
        if (outlineMaskTarget.IsValid()) backend.DeleteRenderTarget(outlineMaskTarget);
        outlineMaskTarget = GpuRenderTargetID();
        outlineMaskTexture = GpuTextureID();
        outlineMaskWidth = 0;
        outlineMaskHeight = 0;

        GpuRenderTargetDesc maskDesc;
        maskDesc.width = viewportWidth;
        maskDesc.height = viewportHeight;
        maskDesc.depthTexture = camera.sceneDepthTexture;
        outlineMaskTarget = backend.CreateRenderTarget(maskDesc);
        if (!outlineMaskTarget.IsValid()) return;

        outlineMaskTexture = backend.GetRenderTargetColorTexture(outlineMaskTarget);
        outlineMaskWidth = viewportWidth;
        outlineMaskHeight = viewportHeight;
    }

    //遮罩只在视口范围里有意义，先整张清成透明且不动共享深度
    RenderPassDesc clearPass;
    clearPass.width = outlineMaskWidth;
    clearPass.height = outlineMaskHeight;
    clearPass.renderTarget = outlineMaskTarget;
    clearPass.clearMode = ClearMode::ColorOnly;
    clearPass.clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    backend.BeginPass(clearPass);
    backend.EndPass();

    //第一遍：把选中几何按描边色平涂进遮罩，只测深度不写深度
    //深度偏移把遮罩几何朝观察者推一点，抵消两次 pass 之间浮点误差造成的深度抖动
    RenderPassDesc maskPass;
    //遮罩目标本身就是视口尺寸、原点为 0，不能再用相机视口原点
    maskPass.x = 0;
    maskPass.y = 0;
    maskPass.width = viewportWidth;
    maskPass.height = viewportHeight;
    maskPass.renderTarget = outlineMaskTarget;
    maskPass.clearMode = ClearMode::None;
    backend.BeginPass(maskPass);
    backend.SetDepthTest(true);
    //场景刚写入的深度与被描边几何同层，必须用 LessEqual 才能通过深度测试
    backend.SetDepthCompare(DepthCompare::LessEqual);
    backend.SetDepthWrite(false);
    backend.SetBlend(false);
    backend.SetCullMode(CullMode::Back);
    backend.SetPolygonOffset(true, -1.0f, -2.0f);
    backend.BindShaderProgram(outlineMaskProgram);

    bool drewAny = false;
    for (const RenderItem& item : visibleSet.renderItems)
    {
        const color tint = FindHighlightTint(item.ens);
        if (tint.a <= 0.0f) continue;

        const GpuMesh* mesh = gpuResourceManager.GetMesh(item.mesh);
        if (!mesh || !mesh->IsValid() || item.indexCount == 0) continue;

        backend.BindVertexInput(mesh->vertexInput);
        backend.SetUniformMatrix4("u_Model", item.localToWorld);
        backend.SetUniformMatrix4("u_ViewProjection", camera.viewProjectionMatrix);
        backend.SetUniformColor("u_Tint", tint);
        backend.DrawIndexed(item.indexStart, item.indexCount);
        drewAny = true;
    }
    backend.EndPass();
    backend.SetPolygonOffset(false, 0.0f, 0.0f);
    backend.SetDepthCompare(DepthCompare::Less);
    backend.SetDepthWrite(true);
    if (!drewAny) return;

    //第二遍：把遮罩轮廓按覆盖度混合到场景缓冲，物体自身保持原色
    RenderPassDesc compositePass;
    //场景缓冲同样是视口尺寸、原点为 0
    compositePass.x = 0;
    compositePass.y = 0;
    compositePass.width = viewportWidth;
    compositePass.height = viewportHeight;
    compositePass.renderTarget = camera.renderTarget;
    compositePass.clearMode = ClearMode::None;
    backend.BeginPass(compositePass);
    backend.SetDepthTest(false);
    backend.SetDepthWrite(false);
    backend.SetBlend(true);
    backend.SetCullMode(CullMode::None);
    backend.BindShaderProgram(outlineCompositeProgram);
    backend.BindTexture(0, outlineMaskTexture);
    backend.SetUniformInt("u_Mask", 0);
    backend.SetUniformVector3("u_TexelSize", vector3 {
        1.0f / static_cast<float32>(viewportWidth),
        1.0f / static_cast<float32>(viewportHeight),
        0.0f });
    backend.SetUniformFloat("u_CoreRadius", OutlineCoreRadius);
    backend.SetUniformFloat("u_GlowRadius", OutlineGlowRadius);
    backend.SetUniformFloat("u_GlowAlpha", OutlineGlowAlpha);
    backend.BindVertexInput(outlineQuad.GetVertexInput());
    backend.DrawIndexed(0, 6);
    backend.EndPass();

    //恢复后续绘制依赖的常规状态
    backend.SetDepthTest(true);
    backend.SetBlend(false);
    backend.SetCullMode(CullMode::Back);
}

//把相机的线性场景缓冲转换到它的最终输出目标，必须在描边与调试线之后调用
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

    //阴影调试视图写出的是显示色，色调映射会改变调色板，只能原样复制
    parameters.mode = forwardPipeline.IsShadowDebugViewActive() ? OutputPass::Mode::Copy : OutputPass::Mode::Display;
    outputPass.Render(parameters);
}
