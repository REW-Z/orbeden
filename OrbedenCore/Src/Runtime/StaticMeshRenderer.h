#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EnsId.h"
#include "Runtime/Resources/Mesh.h"

//静态网格渲染组件
class StaticMeshRenderer : public Component
{
    OBJECT_TYPE_DECLARE(StaticMeshRenderer)

public:
    bool enabled = true;
    Ref<Mesh> mesh;
    uint32 drawLayer = 1u;
    DrawQueue drawQueue = DrawQueue::Opaque;
    bool castShadows = true;
    bool receiveShadows = true;
};
