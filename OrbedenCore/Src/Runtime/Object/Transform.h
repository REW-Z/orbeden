#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EngineTypes.h"
#include "Runtime/Object/Component.h"

//Ens变换组件，保存场景层级和本地变换
class Transform : public Component
{
    OBJECT_TYPE_DECLARE(Transform)
    ORBEDEN_COMPONENT_UNIQUE

private:
    vector3 localPosition;
    quaternion localRotation;
    vector3 localScale = { 1.0f, 1.0f, 1.0f };

public:
    ORBEDEN_BIND_IGNORE
    EnsId parent;
    ORBEDEN_BIND_IGNORE
    EnsId firstChild;
    ORBEDEN_BIND_IGNORE
    EnsId lastChild;
    ORBEDEN_BIND_IGNORE
    EnsId prev;
    ORBEDEN_BIND_IGNORE
    EnsId next;

    ORBEDEN_BIND_IGNORE
    matrix4x4 localMatrix;
    ORBEDEN_BIND_IGNORE
    matrix4x4 worldMatrix;
    ORBEDEN_BIND_IGNORE
    vector3 worldPosition;
    ORBEDEN_BIND_IGNORE
    quaternion worldRotation;

    ORBEDEN_BIND_IGNORE
    bool transformCacheInitialized = false;
    ORBEDEN_BIND_IGNORE
    bool transformDirty = true;

    //读取和修改父级，修改经过 World 层级逻辑。
    EnsId GetParent() const;
    void SetParent(EnsId value);
    vector3 GetWorldPosition() const;
    quaternion GetWorldRotation() const;

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
