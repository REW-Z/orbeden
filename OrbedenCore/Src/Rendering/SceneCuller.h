#pragma once

#include "Rendering/RenderScene.h"

//场景裁剪器，按相机视锥和绘制层筛选当前帧渲染项。
class SceneCuller
{
public:
    //裁剪指定相机可见项，并将结果写入可见集合。
    void Cull(const RenderScene& scene, const RenderCamera& camera, VisibleSet& visibleSet);
};
