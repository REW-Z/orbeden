#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EngineTypes.h"
#include "Runtime/EnsId.h"

//Ens变换组件，保存场景层级和本地变换
class TransformComponent : public Component
{
    OBJECT_TYPE_DECLARE(TransformComponent)

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

    //设置本地位置并通知变换缓存
    void SetLocalPosition(const vector3& value);

    //获取本地旋转
    const quaternion& GetLocalRotation() const;

    //设置本地旋转并通知变换缓存
    void SetLocalRotation(const quaternion& value);

    //获取本地缩放
    const vector3& GetLocalScale() const;

    //设置本地缩放并通知变换缓存
    void SetLocalScale(const vector3& value);
};
