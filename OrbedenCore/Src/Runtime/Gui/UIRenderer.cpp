#include "Runtime/Gui/UIRenderer.h"

#include <vector>

#include "Log/Log.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Gui/UIShaderSources.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/Object/Object.h"
#include "Runtime/Object/Shader.h"
#include "Runtime/Object/Texture2D.h"

namespace
{
    //UI 顶点步长与 UIVertex 的内存布局一致：位置 12 字节、UV 8 字节、顶点色 16 字节。
    constexpr uint32 UIVertexStride = sizeof(float32) * 9;

    //图元着色器的资产 Key：与 Templates/Builtin/Shaders/ui_surface.orbshader 对应。
    constexpr const char* SurfaceShaderKey = "Builtin/Shaders/ui_surface.orbshader";

    //网格连续这么多帧没有被引用才释放：中间可能夹杂只画覆盖层的帧。
    constexpr uint64 MeshGraceFrames = 2;

    //纹理槽位：图元本身与覆盖率。
    constexpr uint32 TextureSlot = 0;
    constexpr uint32 CoverageSlot = 1;

    //图集通道模式，与 UIShaderSources 的 u_ShapeChannelMode 对应。
    constexpr int32 ChannelModeRectangle = 0;
    constexpr int32 ChannelModeRed = 1;
    constexpr int32 ChannelModeAlpha = 2;
    constexpr int32 ChannelModeOpaque = 3;

    //覆盖率采样语义：R8 读 R，RGBA 读 A，其余（RGB 与未知）视为全 1。
    int32 ChannelModeOf(int32 channels)
    {
        if (channels == 1) return ChannelModeRed;
        if (channels == 4) return ChannelModeAlpha;
        return ChannelModeOpaque;
    }

    //画布渲染模式，与托管侧 CanvasRenderMode 一致。
    constexpr uint32 RenderModeScreen = 0;
    constexpr uint32 RenderModeWorldSpace = 1;
    constexpr uint32 RenderModeOffscreen = 2;
}

bool UIRenderer::Initialize(RenderBackend& renderBackend, GpuResourceManager& resourceManager)
{
    if (backend == &renderBackend) return EnsurePrograms();

    Shutdown();
    backend = &renderBackend;
    resources = &resourceManager;
    overlayConversion.Initialize(backend);
    if (!EnsurePrograms()) return false;

    //1×1 白纹理：没有纹理的图元（缺字方框、纯色矩形）取样它。
    const uint8 white[4] = { 255, 255, 255, 255 };
    GpuTextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.channels = 4;
    desc.pixels = white;
    desc.clampToEdge = true;
    whiteTexture = backend->CreateTexture(desc);
    if (!whiteTexture.IsValid())
    {
        Log::Error("UIRenderer: 白纹理创建失败。");
        return false;
    }
    return true;
}

void UIRenderer::Shutdown()
{
    if (!backend) return;

    overlayConversion.Shutdown();
    if (overlayDisplayCopy.IsValid()) backend->DeleteRenderTarget(overlayDisplayCopy);
    if (overlayLinearTarget.IsValid()) backend->DeleteRenderTarget(overlayLinearTarget);
    overlayDisplayCopy = GpuRenderTargetID();
    overlayLinearTarget = GpuRenderTargetID();
    overlayWidth = 0;
    overlayHeight = 0;

    for (auto& entry : meshes) ReleaseMesh(entry.second);
    meshes.clear();
    for (CoverageTarget& target : coveragePool)
    {
        if (target.renderTarget.IsValid()) backend->DeleteRenderTarget(target.renderTarget);
    }
    coveragePool.clear();
    currentCoverage = -1;
    skippedClipDepth = 0;
    syncedFrame = 0;

    if (surfaceProgram.IsValid()) backend->DeleteShaderProgram(surfaceProgram);
    if (coverageProgram.IsValid()) backend->DeleteShaderProgram(coverageProgram);
    if (whiteTexture.IsValid()) backend->DeleteTexture(whiteTexture);
    surfaceProgram = GpuShaderProgramID();
    coverageProgram = GpuShaderProgramID();
    shaderProgramsDirty = true;
    whiteTexture = GpuTextureID();
    backend = nullptr;
    resources = nullptr;
}

bool UIRenderer::EnsurePrograms()
{
    if (!backend) return false;
    if (!shaderProgramsDirty) return surfaceProgram.IsValid() && coverageProgram.IsValid();
    shaderProgramsDirty = false;
    if (!surfaceProgram.IsValid())
    {
        //优先用项目里的 ui_surface.orbshader：用户改它就能改 UI 着色，
        //读不到（项目还没导入这份内置内容）时退回引擎内置源码。
        //先查 Key 是否注册：未注册时 Load 会走导入器并以 Error 记两条（文件不存在 + Key 未注册），
        //而编辑器启动时内容根还不是项目，这条查询必然落空，不该按错误记录。
        GpuShaderProgramDesc desc;
        Shader* asset = ResourceManager::FindRecord(SurfaceShaderKey) != nullptr
            ? ResourceManager::Load<Shader>(SurfaceShaderKey)
            : nullptr;
        if (asset && !asset->passes.empty())
        {
            desc.vertexSource = asset->passes[0].vertexSource.c_str();
            desc.fragmentSource = asset->passes[0].fragmentSource.c_str();
        }
        else
        {
            desc.vertexSource = UIShaderSources::SurfaceVertexSource;
            desc.fragmentSource = UIShaderSources::SurfaceFragmentSource;
        }
        surfaceProgram = backend->CreateShaderProgram(desc);
    }
    if (!coverageProgram.IsValid())
    {
        GpuShaderProgramDesc desc;
        desc.vertexSource = UIShaderSources::CoverageVertexSource;
        desc.fragmentSource = UIShaderSources::CoverageFragmentSource;
        coverageProgram = backend->CreateShaderProgram(desc);
    }
    if (!surfaceProgram.IsValid() || !coverageProgram.IsValid())
    {
        Log::Error("UIRenderer: 内置 shader 创建失败。");
        return false;
    }
    return true;
}

void UIRenderer::SetDisplayLogicalSize(vector2 size)
{
    displayLogicalSize = size;
}

void UIRenderer::InvalidateView(uint64 viewId)
{
    (void)viewId;
    //视图只影响投影矩阵，网格与覆盖率池都与视图无关，无需重建。
}

//重建着色器程序：内容根切换（打开项目）后调用，让项目里的 ui_surface.orbshader 生效。
void UIRenderer::InvalidateShaderPrograms()
{
    if (!backend) return;
    if (surfaceProgram.IsValid()) backend->DeleteShaderProgram(surfaceProgram);
    if (coverageProgram.IsValid()) backend->DeleteShaderProgram(coverageProgram);
    surfaceProgram = GpuShaderProgramID();
    coverageProgram = GpuShaderProgramID();
    shaderProgramsDirty = true;
}

void UIRenderer::InvalidateAll()
{
    currentCoverage = -1;
    skippedClipDepth = 0;
    viewDepths.clear();
}

bool UIRenderer::ReadDepth(uint64 viewId, uint64 viewerId, uint64 presentedFrame,
    int32 x, int32 y, float32& depth) const
{
    depth = 0.0f;
    if (!backend || presentedFrame == 0) return false;

    auto found = viewDepths.find(ViewKey { viewId, viewerId });
    //帧号对不上的查询一律拒绝：问的是已经画出来的那一帧，不是半帧。
    if (found == viewDepths.end() || found->second.frameId != presentedFrame) return false;

    return backend->ReadDepthPixel(found->second.renderTarget, x, y, depth);
}

void UIRenderer::RenderOverlay(const UIOutputTarget& target)
{
    if (!backend || target.width <= 0 || target.height <= 0) return;

    //屏幕画布的首帧视口只能来自这里：没有视口就不会被提交，不被提交就不会有视图快照。
    RetainedGuiFrame::SetDisplaySize(target.width, target.height);
    if (!EnsurePrograms()) return;

    List<RetainedGuiFrame::CanvasView> canvases;
    RetainedGuiFrame::CollectCanvases(canvases);
    SyncMeshes(RetainedGuiFrame::GetPublishedFrame());
    drawCommandCount = 0;

    bool hasOverlay = false;
    for (const RetainedGuiFrame::CanvasView& canvas : canvases)
    {
        if (canvas.submission && canvas.submission->renderMode == RenderModeScreen && canvas.commandCount > 0)
        {
            hasOverlay = true;
            break;
        }
    }
    if (!hasOverlay) return;

    //创建显示副本与线性合成目标
    if (overlayWidth != target.width || overlayHeight != target.height)
    {
        if (overlayDisplayCopy.IsValid()) backend->DeleteRenderTarget(overlayDisplayCopy);
        if (overlayLinearTarget.IsValid()) backend->DeleteRenderTarget(overlayLinearTarget);
        GpuRenderTargetDesc desc;
        desc.width = target.width;
        desc.height = target.height;
        desc.colorOnly = true;
        overlayDisplayCopy = backend->CreateRenderTarget(desc);
        desc.format = GpuRenderTargetFormat::RGBA16F;
        overlayLinearTarget = backend->CreateRenderTarget(desc);
        overlayWidth = target.width;
        overlayHeight = target.height;
    }
    if (!overlayDisplayCopy.IsValid() || !overlayLinearTarget.IsValid())
    {
        overlayWidth = 0;
        overlayHeight = 0;
        Log::Error("UIRenderer: Overlay 颜色合成目标创建失败。");
        return;
    }

    //复制并解码显示颜色
    GpuRenderTargetCopyDesc copy;
    copy.sourceRenderTarget = target.renderTarget;
    copy.destinationRenderTarget = overlayDisplayCopy;
    copy.sourceX = target.x;
    copy.sourceY = target.y;
    copy.width = target.width;
    copy.height = target.height;
    copy.colorOnly = true;
    if (!backend->CopyRenderTarget(copy)) return;
    OutputPass::Parameters conversion;
    conversion.sourceTexture = backend->GetRenderTargetColorTexture(overlayDisplayCopy);
    conversion.destinationRenderTarget = overlayLinearTarget;
    conversion.width = target.width;
    conversion.height = target.height;
    conversion.mode = OutputPass::Mode::DecodeOnly;
    if (!overlayConversion.Render(conversion)) return;

    CanvasTarget canvasTarget;
    canvasTarget.renderTarget = overlayLinearTarget;
    canvasTarget.width = target.width;
    canvasTarget.height = target.height;

    for (const RetainedGuiFrame::CanvasView& canvas : canvases)
    {
        if (!canvas.submission || canvas.submission->renderMode != RenderModeScreen) continue;
        RenderCanvas(canvas, canvasTarget, nullptr);
    }

    //编码合成结果到显示目标
    conversion.sourceTexture = backend->GetRenderTargetColorTexture(overlayLinearTarget);
    conversion.destinationRenderTarget = target.renderTarget;
    conversion.x = target.x;
    conversion.y = target.y;
    conversion.mode = OutputPass::Mode::EncodeOnly;
    overlayConversion.Render(conversion);
}

void UIRenderer::RenderWorldSpace(const RenderCamera& camera)
{
    if (!backend || !EnsurePrograms()) return;
    if (camera.viewportWidth <= 0 || camera.viewportHeight <= 0) return;

    List<RetainedGuiFrame::CanvasView> canvases;
    RetainedGuiFrame::CollectCanvases(canvases);
    SyncMeshes(RetainedGuiFrame::GetPublishedFrame());

    CanvasTarget canvasTarget;
    canvasTarget.renderTarget = camera.renderTarget;
    canvasTarget.width = camera.viewportWidth;
    canvasTarget.height = camera.viewportHeight;
    canvasTarget.depthTest = true;

    //视图快照要带上这台相机的矩阵，托管侧据此反投影命中射线。
    cameraView = camera.viewMatrix;
    cameraProjection = camera.projectionMatrix;
    publishingViewer = camera.ens.id;
    publishingWorldSpace = true;
    int32 displayWidth = 0;
    int32 displayHeight = 0;
    RetainedGuiFrame::GetDisplaySize(displayWidth, displayHeight);
    float32 scaleX = displayWidth > 0 ? displayLogicalSize.x / displayWidth : 1.0f;
    float32 scaleY = displayHeight > 0 ? displayLogicalSize.y / displayHeight : 1.0f;
    publishingOrigin = vector2(camera.viewportX * scaleX,
        (displayHeight - camera.viewportY - camera.viewportHeight) * scaleY);
    publishingSize = vector2(camera.viewportWidth * scaleX, camera.viewportHeight * scaleY);
    for (const RetainedGuiFrame::CanvasView& canvas : canvases)
    {
        if (!canvas.submission || canvas.submission->renderMode != RenderModeWorldSpace) continue;
        if ((canvas.submission->drawLayer & camera.drawLayerMask) == 0) continue;
        //世界空间：提交里的矩阵是画布本地到世界的变换，这里再乘相机的视图投影。
        RenderCanvas(canvas, canvasTarget, &camera.viewProjectionMatrix);
    }
    publishingWorldSpace = false;
    publishingViewer = 0;
}

//按编辑相机投影绘制屏幕画布
void UIRenderer::RenderEditorPreview(const RenderCamera& camera)
{
    if (!backend || camera.viewportWidth <= 0 || camera.viewportHeight <= 0) return;
    RetainedGuiFrame::SetDisplaySize(camera.viewportWidth, camera.viewportHeight);
    if (!EnsurePrograms()) return;

    List<RetainedGuiFrame::CanvasView> canvases;
    RetainedGuiFrame::CollectCanvases(canvases);
    SyncMeshes(RetainedGuiFrame::GetPublishedFrame());
    CanvasTarget target;
    target.renderTarget = camera.renderTarget;
    target.width = camera.viewportWidth;
    target.height = camera.viewportHeight;
    target.depthTest = true;
    cameraView = camera.viewMatrix;
    cameraProjection = camera.projectionMatrix;
    publishingEditorPreview = true;
    for (const RetainedGuiFrame::CanvasView& canvas : canvases)
    {
        if (!canvas.submission || canvas.submission->renderMode != RenderModeScreen) continue;
        RenderCanvas(canvas, target, &camera.viewProjectionMatrix);
    }
    publishingEditorPreview = false;
}

void UIRenderer::RenderOffscreen()
{
    //离屏画布先于相机绘制：它们的输出纹理可能被世界空间或屏幕画布采样。
    if (!backend || !resources || !EnsurePrograms()) return;

    List<RetainedGuiFrame::CanvasView> canvases;
    RetainedGuiFrame::CollectCanvases(canvases);
    SyncMeshes(RetainedGuiFrame::GetPublishedFrame());

    for (const RetainedGuiFrame::CanvasView& canvas : canvases)
    {
        if (!canvas.submission || canvas.submission->renderMode != RenderModeOffscreen) continue;
        if (canvas.submission->width <= 0 || canvas.submission->height <= 0) continue;

        Object* object = Object::FindObjectById(canvas.submission->outputTextureObjectId);
        Texture2D* output = object ? object->Cast<Texture2D>() : nullptr;
        if (!output || !output->IsRenderTarget())
        {
            if (!offscreenWarned)
            {
                Log::Warning("UIRenderer: 离屏画布缺少输出纹理，本帧跳过。");
                offscreenWarned = true;
            }
            continue;
        }

        GpuRenderTargetID renderTarget = resources->GetTextureRenderTarget(output);
        if (!renderTarget.IsValid()) continue;

        CanvasTarget canvasTarget;
        canvasTarget.renderTarget = renderTarget;
        canvasTarget.x = 0;
        canvasTarget.y = 0;
        canvasTarget.width = output->width;
        canvasTarget.height = output->height;
        //输出是线性预乘，清成透明黑；首帧没有旧内容可保留。
        canvasTarget.clearOnFirstPass = true;
        RenderCanvas(canvas, canvasTarget, nullptr);
    }
}

void UIRenderer::RenderCanvas(const RetainedGuiFrame::CanvasView& canvas, const CanvasTarget& target,
    const matrix4x4* canvasWorld)
{
    if (!canvas.submission) return;
    if (canvas.commandCount <= 0)
    {
        //没有命令的画布仍要清屏：离屏输出必须每帧从透明黑开始，
        //托管侧把循环依赖的画布提交成空命令正是靠这一点。
        if (target.clearOnFirstPass) ClearTarget(target);
        return;
    }

    currentViewProjection = canvasWorld
        ? RenderMath::Mul(*canvasWorld, canvas.submission->viewProjection)
        : canvas.submission->viewProjection;
    currentTarget = target;
    //只有离屏输出的第一次进入才清屏；裁剪层进出会用 None 重新进入同一目标。
    clearNextPass = target.clearOnFirstPass;

    //每帧第一次渲染时清空上一帧的视图快照，然后逐块画布发布本帧尺寸。
    uint64 publishedFrame = canvas.submission->frameId;
    if (publishedFrame != viewPublishFrame)
    {
        viewPublishFrame = publishedFrame;
        RetainedGuiFrame::BeginViewPublish();
    }
    uint32 viewFlags = target.clearOnFirstPass ? 0u : static_cast<uint32>(UIViewFlags::Presented);
    //发布世界空间相机快照
    if (publishingWorldSpace) viewFlags |= static_cast<uint32>(UIViewFlags::WorldSpaceCamera);
    if (publishingEditorPreview) viewFlags |= static_cast<uint32>(UIViewFlags::EditorPreview);
    //世界空间画布的视图原点与相机来自本次绘制；屏幕画布用零原点和单位矩阵。
    vector2 origin = publishingWorldSpace ? publishingOrigin : vector2(0.0f, 0.0f);
    vector2 logicalSize = publishingWorldSpace ? publishingSize : displayLogicalSize;
    matrix4x4 viewMatrix = publishingWorldSpace || publishingEditorPreview ? cameraView : matrix4x4();
    matrix4x4 projectionMatrix = publishingWorldSpace || publishingEditorPreview ? cameraProjection : matrix4x4();
    RetainedGuiFrame::PublishView(canvas.submission->canvasId, target.width, target.height, viewFlags,
        origin, logicalSize, viewMatrix, projectionMatrix, publishingViewer);

    //记录视图的深度来源，供命中快照回读；离屏输出没有场景深度。
    if (!target.clearOnFirstPass)
    {
        //同一块画布在世界空间下每台相机各有一条记录，按观察者分槽。
        ViewDepth& record = viewDepths[ViewKey { canvas.submission->viewId, publishingViewer }];
        record.frameId = canvas.submission->frameId;
        record.renderTarget = target.renderTarget;
    }

    RestoreSurfaceState();

    for (int32 index = 0; index < canvas.commandCount; ++index)
    {
        RenderCommand(canvas.commands[index], canvas.matrices, canvas.matrixCount);
    }

    //画布结束后不留半开的裁剪栈：正常配对时这里已经是空的。
    while (currentCoverage >= 0 || skippedClipDepth > 0) PopCoverage();

    backend->BindVertexInput(GpuVertexInputID());
    backend->BindShaderProgram(GpuShaderProgramID());
    backend->EndPass();
}

void UIRenderer::RenderCommand(const UIDrawCommand& command, const matrix4x4* matrices, int32 matrixCount)
{
    switch (static_cast<UIDrawCommandKind>(command.commandKind))
    {
    case UIDrawCommandKind::Draw:
        //裁剪目标分配失败时整段子树都不画，不输出没有遮罩的替代结果。
        if (skippedClipDepth > 0) return;
        DrawCommand(command, matrices, matrixCount);
        return;
    case UIDrawCommandKind::PushRectangle:
    case UIDrawCommandKind::PushImageAlpha:
        PushCoverage(command, matrices, matrixCount);
        return;
    case UIDrawCommandKind::Pop:
        PopCoverage();
        return;
    default:
        return;
    }
}

void UIRenderer::DrawCommand(const UIDrawCommand& command, const matrix4x4* matrices, int32 matrixCount)
{
    if (command.indexCount == 0) return;
    if (!matrices || command.matrixIndex >= static_cast<uint32>(matrixCount)) return;

    //检查字体图集纹理
    const auto kind = static_cast<UIMaterialKind>(command.materialKind);
    const bool glyph = kind == UIMaterialKind::Bitmap || kind == UIMaterialKind::SDF || kind == UIMaterialKind::MSDF;
    GpuTextureID texture;
    if (glyph)
    {
        Object* object = Object::FindObjectById(command.textureObjectId);
        Texture2D* source = object ? object->Cast<Texture2D>() : nullptr;
        if (!source || !resources) return;
        texture = resources->GetTexture(source);
        if (!texture.IsValid()) return;
    }
    else texture = ResolveTexture(command.textureObjectId);

    const MeshGpu* mesh = EnsureMesh(command.meshId, syncedFrame);
    if (!mesh) return;

    backend->BindVertexInput(mesh->vertexInput);
    BindCommandProgram(command);
    backend->SetUniformMatrix4("u_Model", matrices[command.matrixIndex]);
    backend->SetUniformInt("u_MaterialKind", static_cast<int32>(command.materialKind));
    backend->SetUniformFloat("u_DistanceRange", command.distanceRange);
    backend->SetUniformColor("u_Tint", command.tint);
    backend->SetUniformInt("u_CoverageEnabled", currentCoverage >= 0 ? 1 : 0);
    if (currentCoverage >= 0)
    {
        backend->BindTexture(CoverageSlot, coveragePool[currentCoverage].texture);
        backend->SetUniformInt("u_Coverage", static_cast<int32>(CoverageSlot));
    }

    backend->BindTexture(TextureSlot, texture);
    backend->SetUniformInt("u_Texture", static_cast<int32>(TextureSlot));
    backend->DrawIndexed(command.firstIndex, command.indexCount);
    ++drawCommandCount;
}

void UIRenderer::PushCoverage(const UIDrawCommand& command, const matrix4x4* matrices, int32 matrixCount)
{
    //已经在跳过状态里：只把嵌套层数记下来，出栈时逐层减回去。
    if (skippedClipDepth > 0)
    {
        ++skippedClipDepth;
        return;
    }
    if (!matrices || command.matrixIndex >= static_cast<uint32>(matrixCount))
    {
        skippedClipDepth = 1;
        return;
    }

    int32 parent = currentCoverage;
    int32 index = AcquireCoverage(currentTarget.width, currentTarget.height);
    if (index < 0)
    {
        Log::Error("UIRenderer: 覆盖率目标分配失败，跳过整段裁剪子树。");
        skippedClipDepth = 1;
        return;
    }

    CoverageTarget& coverage = coveragePool[index];
    coverage.parent = parent;

    //取目标清零，再把裁剪形状投影进去：写的是 父覆盖率 * 本层覆盖率。
    RenderPassDesc pass;
    pass.renderTarget = coverage.renderTarget;
    pass.x = 0;
    pass.y = 0;
    pass.width = coverage.width;
    pass.height = coverage.height;
    pass.clearMode = ClearMode::SolidColor;
    pass.clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    backend->BeginPass(pass);

    backend->SetDepthTest(false);
    backend->SetDepthWrite(false);
    backend->SetBlend(false);
    backend->SetCullMode(CullMode::None);
    backend->BindShaderProgram(coverageProgram);
    backend->SetUniformMatrix4("u_ViewProjection", currentViewProjection);
    backend->SetUniformMatrix4("u_Model", matrices[command.matrixIndex]);
    //覆盖率 Pass 的目标就是视口本身，原点为零。
    backend->SetUniformVector3("u_ViewportSize", vector3(static_cast<float32>(coverage.width),
        static_cast<float32>(coverage.height), 0.0f));

    int32 channelMode = ChannelModeRectangle;
    GpuTextureID shapeTexture = whiteTexture;
    if (static_cast<UIDrawCommandKind>(command.commandKind) == UIDrawCommandKind::PushImageAlpha)
    {
        Object* object = Object::FindObjectById(command.textureObjectId);
        Texture2D* source = object ? object->Cast<Texture2D>() : nullptr;
        if (!source)
        {
            //Alpha 纹理缺失时覆盖率为零：跳过整段子树而不是画成无遮罩。
            backend->EndPass();
            ReleaseCoverage(index);
            skippedClipDepth = 1;
            return;
        }
        shapeTexture = ResolveTexture(command.textureObjectId);
        channelMode = ChannelModeOf(source->channels);
    }

    backend->BindTexture(TextureSlot, shapeTexture);
    backend->SetUniformInt("u_ShapeTexture", static_cast<int32>(TextureSlot));
    backend->SetUniformInt("u_ShapeChannelMode", channelMode);
    backend->SetUniformInt("u_ParentCoverageEnabled", parent >= 0 ? 1 : 0);
    if (parent >= 0)
    {
        backend->BindTexture(CoverageSlot, coveragePool[parent].texture);
        backend->SetUniformInt("u_ParentCoverage", static_cast<int32>(CoverageSlot));
    }

    const MeshGpu* shape = EnsureMesh(command.meshId, syncedFrame);
    if (shape && command.indexCount != 0)
    {
        backend->BindVertexInput(shape->vertexInput);
        backend->DrawIndexed(command.firstIndex, command.indexCount);
    }
    backend->BindVertexInput(GpuVertexInputID());
    backend->EndPass();

    currentCoverage = index;
    //裁剪 Pass 换了目标与状态，回到画布目标继续画。
    RestoreSurfaceState();
}

void UIRenderer::PopCoverage()
{
    if (skippedClipDepth > 0)
    {
        --skippedClipDepth;
        return;
    }
    if (currentCoverage < 0) return;

    int32 released = currentCoverage;
    currentCoverage = coveragePool[released].parent;
    ReleaseCoverage(released);
    //目标与绑定都变了，恢复画布状态。
    RestoreSurfaceState();
}

void UIRenderer::ClearTarget(const CanvasTarget& target)
{
    RenderPassDesc pass;
    pass.renderTarget = target.renderTarget;
    pass.x = target.x;
    pass.y = target.y;
    pass.width = target.width;
    pass.height = target.height;
    pass.clearMode = ClearMode::SolidColor;
    pass.clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    backend->BeginPass(pass);
    backend->EndPass();
}

void UIRenderer::RestoreSurfaceState()
{
    RenderPassDesc pass;
    pass.renderTarget = currentTarget.renderTarget;
    pass.x = currentTarget.x;
    pass.y = currentTarget.y;
    pass.width = currentTarget.width;
    pass.height = currentTarget.height;
    if (clearNextPass)
    {
        //离屏输出从透明黑开始；透明黑在预乘语义下四个分量都是零。
        pass.clearMode = ClearMode::SolidColor;
        pass.clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    }
    else
    {
        pass.clearMode = ClearMode::None;
    }
    clearNextPass = false;
    backend->BeginPass(pass);

    backend->SetDepthTest(currentTarget.depthTest);
    backend->SetDepthCompare(DepthCompare::LessEqual);
    backend->SetDepthWrite(false);
    backend->SetBlend(true);
    backend->SetBlendMode(BlendMode::PremultipliedAlpha);
    backend->SetCullMode(CullMode::None);
    backend->BindShaderProgram(surfaceProgram);
    boundProgram = surfaceProgram;
    SetFrameUniforms();
}

//矩阵与视口这些帧级 uniform：换绘制程序之后必须重设一次。
void UIRenderer::SetFrameUniforms()
{
    backend->SetUniformMatrix4("u_ViewProjection", currentViewProjection);
    backend->SetUniformVector3("u_ViewportSize", vector3(static_cast<float32>(currentTarget.width),
        static_cast<float32>(currentTarget.height), 0.0f));
    backend->SetUniformVector3("u_ViewportOrigin", vector3(static_cast<float32>(currentTarget.x),
        static_cast<float32>(currentTarget.y), 0.0f));
}

//按命令的显式材质切换绘制程序：材质参数先绑，保留参数随后覆盖回去。
void UIRenderer::BindCommandProgram(const UIDrawCommand& command)
{
    const GpuShaderPass* pass = GetCommandMaterialPass(command);
    if (pass && pass->shaderProgram.IsValid())
    {
        if (!(boundProgram == pass->shaderProgram))
        {
            backend->BindShaderProgram(pass->shaderProgram);
            boundProgram = pass->shaderProgram;
            SetFrameUniforms();
        }

        //材质自己的纹理从保留槽之后开始绑；颜色与浮点按名字绑。
        const GpuMaterial* material = GetCommandMaterial(command);
        if (material)
        {
            uint32 slot = MaterialTextureSlotBase;
            for (const GpuMaterialTextureBinding& binding : material->textureBindings)
            {
                if (IsReservedUniform(binding.uniformName)) continue;
                backend->SetUniformInt(binding.uniformName.c_str(), static_cast<int32>(slot));
                backend->SetUniformInt(binding.presenceUniformName.c_str(), binding.hasTexture ? 1 : 0);
                backend->BindTexture(slot, binding.hasTexture ? binding.texture : GpuTextureID());
                ++slot;
            }
            for (const GpuMaterialColorBinding& binding : material->colorBindings)
            {
                if (IsReservedUniform(binding.uniformName)) continue;
                backend->SetUniformColor(binding.uniformName.c_str(), binding.value);
            }
            for (const GpuMaterialFloatBinding& binding : material->floatBindings)
            {
                if (IsReservedUniform(binding.uniformName)) continue;
                backend->SetUniformFloat(binding.uniformName.c_str(), binding.value);
            }
        }
        return;
    }

    //没有材质、材质无效或材质 shader 不可用：退回内置 UI 程序。
    if (!(boundProgram == surfaceProgram))
    {
        backend->BindShaderProgram(surfaceProgram);
        boundProgram = surfaceProgram;
        SetFrameUniforms();
    }
}

//解析命令引用的材质；标识无效或类型不符时返回空并只报一次诊断。
const GpuMaterial* UIRenderer::GetCommandMaterial(const UIDrawCommand& command)
{
    if (command.materialObjectId == 0 || !resources) return nullptr;

    Object* object = Object::FindObjectById(static_cast<int32>(command.materialObjectId));
    Material* source = object ? object->Cast<Material>() : nullptr;
    if (!source)
    {
        ReportInvalidMaterial(command.materialObjectId, "对象不存在或不是材质");
        return nullptr;
    }

    const GpuMaterial* material = resources->GetMaterial(source);
    if (!material || !material->shader || material->shader->passes.empty())
    {
        ReportInvalidMaterial(command.materialObjectId, "材质没有可用的 shader");
        return nullptr;
    }
    return material;
}

//取材质第一个 Pass 的单绘制程序；不可用时按无效材质处理。
const GpuShaderPass* UIRenderer::GetCommandMaterialPass(const UIDrawCommand& command)
{
    const GpuMaterial* material = GetCommandMaterial(command);
    if (!material) return nullptr;

    const GpuShaderPass& pass = material->shader->passes[0];
    if (!pass.shaderProgram.IsValid())
    {
        ReportInvalidMaterial(command.materialObjectId, "shader 的第一个 Pass 没有编译成功");
        return nullptr;
    }
    return &pass;
}

//不兼容材质的诊断每个来源只报一次。
void UIRenderer::ReportInvalidMaterial(uint64 materialObjectId, const char* reason)
{
    int32 key = static_cast<int32>(materialObjectId);
    if (!invalidMaterials.insert(key).second) return;
    Log::Error((std::string("UIRenderer: 材质 ") + std::to_string(materialObjectId)
        + " 不可用，回退到内置 UI 材质（" + reason + "）。").c_str());
}

//矩阵、视口与裁剪这些保留参数由提交器写入，材质参数不能覆盖它们。
bool UIRenderer::IsReservedUniform(const std::string& name)
{
    static const char* Reserved[] = {
        "u_Model", "u_ViewProjection", "u_Texture", "u_Tint", "u_Coverage", "u_CoverageEnabled",
        "u_DistanceRange", "u_MaterialKind", "u_ViewportSize", "u_ViewportOrigin",
    };
    for (const char* candidate : Reserved)
    {
        if (name == candidate) return true;
    }
    return false;
}

GpuTextureID UIRenderer::ResolveTexture(int32 textureObjectId) const
{
    if (textureObjectId == 0 || !resources) return whiteTexture;
    Object* object = Object::FindObjectById(textureObjectId);
    Texture2D* source = object ? object->Cast<Texture2D>() : nullptr;
    if (!source) return whiteTexture;

    GpuTextureID texture = resources->GetTexture(source);
    return texture.IsValid() ? texture : whiteTexture;
}

int32 UIRenderer::AcquireCoverage(int32 width, int32 height)
{
    if (width <= 0 || height <= 0) return -1;

    //同尺寸的空闲目标优先复用；池子按视口尺寸分类。
    for (usize index = 0; index < coveragePool.size(); ++index)
    {
        CoverageTarget& candidate = coveragePool[index];
        if (!candidate.inUse && candidate.width == width && candidate.height == height)
        {
            candidate.inUse = true;
            return static_cast<int32>(index);
        }
    }

    GpuRenderTargetDesc desc;
    desc.width = width;
    desc.height = height;
    desc.colorOnly = true;
    desc.format = GpuRenderTargetFormat::R8;
    GpuRenderTargetID renderTarget = backend->CreateRenderTarget(desc);
    if (!renderTarget.IsValid()) return -1;

    CoverageTarget created;
    created.renderTarget = renderTarget;
    created.texture = backend->GetRenderTargetColorTexture(renderTarget);
    created.width = width;
    created.height = height;
    created.inUse = true;
    if (!created.texture.IsValid())
    {
        backend->DeleteRenderTarget(renderTarget);
        return -1;
    }
    coveragePool.push_back(created);
    return static_cast<int32>(coveragePool.size() - 1);
}

void UIRenderer::ReleaseCoverage(int32 index)
{
    if (index < 0 || index >= static_cast<int32>(coveragePool.size())) return;
    coveragePool[index].inUse = false;
    coveragePool[index].parent = -1;
}

void UIRenderer::SyncMeshes(uint64 frameId)
{
    if (frameId == 0 || frameId == syncedFrame) return;
    syncedFrame = frameId;

    //连续多帧没有出现的网格释放掉；网格标识不会复用，晚到的命令不会命中新网格。
    std::vector<uint64> stale;
    for (auto& entry : meshes)
    {
        if (entry.second.lastUsedFrame + MeshGraceFrames >= frameId) continue;
        ReleaseMesh(entry.second);
        stale.push_back(entry.first);
    }
    for (uint64 id : stale) meshes.erase(id);
}

const UIRenderer::MeshGpu* UIRenderer::EnsureMesh(uint64 meshId, uint64 frameId)
{
    uint64 revision = 0;
    const UIVertex* vertices = nullptr;
    int32 vertexCount = 0;
    const uint32* indices = nullptr;
    int32 indexCount = 0;
    if (!RetainedGuiFrame::GetMesh(meshId, revision, vertices, vertexCount, indices, indexCount))
        return nullptr;
    if (vertexCount == 0 || indexCount == 0) return nullptr;

    MeshGpu& mesh = meshes[meshId];
    const usize vertexBytes = static_cast<usize>(vertexCount) * UIVertexStride;
    const bool needsUpload = !mesh.valid || mesh.revision != revision
        || vertexBytes > mesh.vertexCapacity || static_cast<uint32>(indexCount) > mesh.indexCapacity;
    mesh.lastUsedFrame = frameId;
    if (!needsUpload) return &mesh;

    //容量不够就整体重建；只是内容变了则流式覆盖，避免每帧重新分配。
    if (!mesh.valid || vertexBytes > mesh.vertexCapacity)
    {
        if (mesh.valid) ReleaseMesh(mesh);
        GpuBufferDesc vertexDesc;
        vertexDesc.size = vertexBytes;
        vertexDesc.usage = GpuBufferUsage::Stream;
        mesh.vertexBuffer = backend->CreateVertexBuffer(vertexDesc);
        mesh.vertexCapacity = vertexBytes;

        GpuBufferDesc indexDesc;
        indexDesc.size = static_cast<usize>(indexCount) * sizeof(uint32);
        indexDesc.usage = GpuBufferUsage::Stream;
        mesh.indexBuffer = backend->CreateIndexBuffer(indexDesc);
        mesh.indexCapacity = static_cast<uint32>(indexCount);

        GpuVertexInputDesc inputDesc;
        inputDesc.vertexBuffer = mesh.vertexBuffer;
        inputDesc.indexBuffer = mesh.indexBuffer;
        inputDesc.stride = UIVertexStride;
        inputDesc.layout = GpuVertexLayout::UI;
        mesh.vertexInput = backend->CreateVertexInput(inputDesc);
        mesh.valid = mesh.vertexBuffer.IsValid() && mesh.indexBuffer.IsValid() && mesh.vertexInput.IsValid();
        if (!mesh.valid)
        {
            ReleaseMesh(mesh);
            return nullptr;
        }
    }
    else if (!mesh.valid)
    {
        return nullptr;
    }

    bool uploaded = backend->UploadVertexBuffer(mesh.vertexBuffer, vertices, vertexBytes, mesh.vertexCapacity);
    uploaded &= backend->UploadIndexBuffer(mesh.indexBuffer, indices, static_cast<uint32>(indexCount),
        mesh.indexCapacity);
    if (!uploaded)
    {
        Log::Error("UIRenderer: 网格上传失败。");
        ReleaseMesh(mesh);
        return nullptr;
    }

    mesh.revision = revision;
    return &mesh;
}

void UIRenderer::ReleaseMesh(MeshGpu& mesh)
{
    if (!backend) return;
    if (mesh.vertexInput.IsValid()) backend->DeleteVertexInput(mesh.vertexInput);
    if (mesh.vertexBuffer.IsValid()) backend->DeleteVertexBuffer(mesh.vertexBuffer);
    if (mesh.indexBuffer.IsValid()) backend->DeleteIndexBuffer(mesh.indexBuffer);
    mesh.vertexInput = GpuVertexInputID();
    mesh.vertexBuffer = GpuVertexBufferID();
    mesh.indexBuffer = GpuIndexBufferID();
    mesh.vertexCapacity = 0;
    mesh.indexCapacity = 0;
    mesh.revision = 0;
    mesh.valid = false;
}
