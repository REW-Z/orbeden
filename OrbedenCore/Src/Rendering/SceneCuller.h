#pragma once

#include "Rendering/RenderScene.h"

//场景裁剪
class SceneCuller
{
public:
    //裁剪指定相机可见项
    void Cull(const RenderScene& scene, const RenderCamera& camera, VisibleSet& visibleSet);
};

