#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/GpuResourceManager.h"
#include "Rendering/RenderScene.h"

//Forward 渲染管线
class ForwardPipeline
{
private:
    RenderBackend* backend = nullptr;

public:
    //初始化管线
    void Initialize(RenderBackend* renderBackend);

    //渲染可见集合
    void Render(const VisibleSet& visibleSet, GpuResourceManager& resources);
};

