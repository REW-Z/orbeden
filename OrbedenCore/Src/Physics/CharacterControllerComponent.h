#pragma once

#include "Physics/PhysicsTypes.h"

//PhysX CCT 角色控制器组件，位置使用实体变换的脚底坐标
class CharacterControllerComponent : public Component
{
    OBJECT_TYPE_DECLARE(CharacterControllerComponent)

public:
    bool enabled = true;
    CharacterControllerShape shape = CharacterControllerShape::Capsule;
    float32 radius = 0.5f;
    float32 height = 1.0f;
    vector3 halfExtents = { 0.5f, 1.0f, 0.5f };
    float32 stepOffset = 0.3f;
    float32 contactOffset = 0.05f;
    float32 slopeLimit = 0.707f;
    float32 minMoveDistance = 0.001f;
    uint32 collisionLayer = 1u;
    uint32 collisionMask = 0xFFFFFFFFu;
};
