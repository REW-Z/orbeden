#pragma once

#include "Rendering/RenderScene.h"
#include "Rendering/SpaceCache.h"

//从 World 构建当前帧渲染场景
class RenderSceneBuilder
{
public:
    //构建当前帧渲染场景
    void Build(World& world, SpaceCache& spaceCache, int32 viewportWidth, int32 viewportHeight, RenderScene& scene);
};

