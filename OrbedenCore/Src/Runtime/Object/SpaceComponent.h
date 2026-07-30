#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EngineTypes.h"
#include "Runtime/EnsId.h"

//Ens空间组件，保存空间层级和本地变换
class SpaceComponent : public Component
{
    OBJECT_TYPE_DECLARE(SpaceComponent)

private:
    vector3 localPosition;
    quaternion localRotation;
    vector3 localScale = { 1.0f, 1.0f, 1.0f };

public:
    EnsId parent;
    EnsId firstChild;
    EnsId lastChild;
    EnsId prev;
    EnsId next;

    matrix4x4 localMatrix;
    matrix4x4 worldMatrix;
    vector3 worldPosition;
    quaternion worldRotation;

    bool transformCacheInitialized = false;
    bool transformDirty = true;

    //获取本地位置
    const vector3& GetLocalPosition() const;

    //设置本地位置并通知空间缓存
    void SetLocalPosition(const vector3& value);

    //获取本地旋转
    const quaternion& GetLocalRotation() const;

    //设置本地旋转并通知空间缓存
    void SetLocalRotation(const quaternion& value);

    //获取本地缩放
    const vector3& GetLocalScale() const;

    //设置本地缩放并通知空间缓存
    void SetLocalScale(const vector3& value);
};
