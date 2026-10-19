#include "Editor/EditorSelectionOutline.h"

#include "Log/Log.h"
#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/ColorSpace.h"
#include "Rendering/GpuResourceManager.h"

namespace
{
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

/// <summary>设置当前选择及其后代的描边颜色。</summary>
void EditorSelectionOutline::SetHighlights(const List<SelectionHighlight>& highlights)
{
    //描边色同样来自编辑器界面，语义是显示色
    selectionHighlights = highlights;
    for (SelectionHighlight& highlight : selectionHighlights)
    {
        highlight.tint = ColorSpace::SrgbToLinear(highlight.tint);
    }
}

//查找本帧提交的描边颜色
color EditorSelectionOutline::FindHighlightTint(EnsId ens) const
{
    for (const SelectionHighlight& highlight : selectionHighlights)
    {
        if (highlight.ens == ens) return highlight.tint;
    }
    return color { 0.0f, 0.0f, 0.0f, 0.0f };
}

//准备选择描边所需的内置 shader 和全屏四边形
bool EditorSelectionOutline::PrepareResources(RenderBackend& backend)
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

    //创建描边全屏几何
    outlineQuad.Initialize(&backend);
    const bool ready = outlineMaskProgram.IsValid() && outlineCompositeProgram.IsValid() && outlineQuad.EnsureReady();
    if (!ready && !outlineWarned)
    {
        Log::Error("EditorSelectionOutline setup failed: outline resources are unavailable.");
        outlineWarned = true;
    }
    return ready;
}

//释放选择描边持有的后端资源
void EditorSelectionOutline::ReleaseResources(RenderBackend& backend)
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
void EditorSelectionOutline::Render(RenderBackend& backend, GpuResourceManager& gpuResourceManager,
    const RenderCamera& camera, const VisibleSet& visibleSet)
{
    if (selectionHighlights.empty()) return;

    const int32 viewportWidth = camera.viewportWidth;
    const int32 viewportHeight = camera.viewportHeight;
    if (viewportWidth <= 0 || viewportHeight <= 0) return;
    if (!PrepareResources(backend)) return;

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

    //更新当前相机的深度附件
    backend.AttachRenderTargetDepthTexture(outlineMaskTarget, camera.sceneDepthTexture);

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
    backend.GetPipelineState().depthTest = true;
    //场景刚写入的深度与被描边几何同层，必须用 LessEqual 才能通过深度测试
    backend.GetPipelineState().depthCompare = DepthCompare::LessEqual;
    backend.GetPipelineState().depthWrite = false;
    backend.GetPipelineState().blend = false;
    backend.GetPipelineState().cull = CullMode::Back;
    backend.GetPipelineState().depthBias = true;
    backend.GetPipelineState().depthBiasSlope = -1.0f;
    backend.GetPipelineState().depthBiasConstant = -2.0f;
    backend.BindShaderProgram(outlineMaskProgram);

    bool drewAny = false;
    for (const RenderItem& item : visibleSet.renderItems)
    {
        const color tint = FindHighlightTint(item.ens);
        if (tint.a <= 0.0f) continue;

        const GpuMesh* mesh = gpuResourceManager.GetMesh(item.mesh);
        if (!mesh || !mesh->IsValid() || item.indexCount == 0) continue;

        backend.BindVertexInput(mesh->vertexInput);
        backend.GetParameters().SetMatrix4("u_Model", item.localToWorld);
        backend.GetParameters().SetMatrix4("u_ViewProjection", camera.viewProjectionMatrix);
        backend.GetParameters().SetColor("u_Tint", tint);
        backend.DrawIndexed(item.indexStart, item.indexCount);
        drewAny = true;
    }
    backend.EndPass();
    backend.GetPipelineState().depthBias = false;
    backend.GetPipelineState().depthBiasSlope = 0.0f;
    backend.GetPipelineState().depthBiasConstant = 0.0f;
    backend.GetPipelineState().depthCompare = DepthCompare::Less;
    backend.GetPipelineState().depthWrite = true;
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
    backend.GetPipelineState().depthTest = false;
    backend.GetPipelineState().depthWrite = false;
    backend.GetPipelineState().blend = true;
    backend.GetPipelineState().cull = CullMode::None;
    backend.BindShaderProgram(outlineCompositeProgram);
    backend.BindTexture(0, outlineMaskTexture);
    backend.GetParameters().SetInt("u_Mask", 0);
    backend.GetParameters().SetVector3("u_TexelSize", vector3 {
        1.0f / static_cast<float32>(viewportWidth),
        1.0f / static_cast<float32>(viewportHeight),
        0.0f });
    backend.GetParameters().SetFloat("u_CoreRadius", OutlineCoreRadius);
    backend.GetParameters().SetFloat("u_GlowRadius", OutlineGlowRadius);
    backend.GetParameters().SetFloat("u_GlowAlpha", OutlineGlowAlpha);
    backend.BindVertexInput(outlineQuad.GetVertexInput());
    backend.DrawIndexed(0, 6);
    backend.EndPass();

    //恢复后续绘制依赖的常规状态
    backend.GetPipelineState().depthTest = true;
    backend.GetPipelineState().blend = false;
    backend.GetPipelineState().cull = CullMode::Back;
}
