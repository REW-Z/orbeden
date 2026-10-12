#pragma once

#include "Rendering/FullscreenQuad.h"
#include "Rendering/RenderScene.h"

class GpuResourceManager;
class RenderBackend;

//SceneView 选中描边，持有编辑器专用的颜色、着色器和遮罩资源
class EditorSelectionOutline
{
public:
    struct SelectionHighlight
    {
        EnsId ens;
        color tint;
    };

private:
    GpuShaderProgramID outlineMaskProgram;
    GpuShaderProgramID outlineCompositeProgram;
    GpuRenderTargetID outlineMaskTarget;
    GpuTextureID outlineMaskTexture;
    FullscreenQuad outlineQuad;
    int32 outlineMaskWidth = 0;
    int32 outlineMaskHeight = 0;
    bool outlineWarned = false;
    List<SelectionHighlight> selectionHighlights;

public:
    /// <summary>设置当前选择及其后代的描边颜色。</summary>
    void SetHighlights(const List<SelectionHighlight>& highlights);

    /// <summary>绘制 EditorCamera 的选中遮罩并合成轮廓。</summary>
    void Render(RenderBackend& backend, GpuResourceManager& gpuResourceManager,
        const RenderCamera& camera, const VisibleSet& visibleSet);

    /// <summary>释放描边持有的 GPU 资源。</summary>
    void ReleaseResources(RenderBackend& backend);

private:
    /// <summary>创建描边着色器与全屏几何。</summary>
    bool PrepareResources(RenderBackend& backend);

    /// <summary>获取选中物体的描边颜色。</summary>
    color FindHighlightTint(EnsId ens) const;
};
