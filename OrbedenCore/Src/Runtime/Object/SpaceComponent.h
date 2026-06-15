#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EngineTypes.h"
#include "Runtime/EnsId.h"

//Ens空间组件，保存空间层级和本地变换
class SpaceComponent : public Component
{
    OBJECT_TYPE_DECLARE(SpaceComponent)

public:
    EnsId parent;
    EnsId firstChild;
    EnsId lastChild;
    EnsId prev;
    EnsId next;

    vector3 localPosition;
    quaternion localRotation;
    vector3 localScale = { 1.0f, 1.0f, 1.0f };

    matrix4x4 localMatrix;
    matrix4x4 worldMatrix;
    vector3 worldPosition;
    quaternion worldRotation;

    vector3 cachedLocalPosition;
    quaternion cachedLocalRotation;
    vector3 cachedLocalScale = { 1.0f, 1.0f, 1.0f };
    EnsId cachedParent;
    bool transformCacheInitialized = false;
    bool transformDirty = true;
};
