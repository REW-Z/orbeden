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

    //局部位置的派生覆盖：布局等运行时驱动写入，不持久化、不参与序列化。
    //ownerToken 记录当前持有者，非零且匹配才允许更新或清除。
    uint64 derivedOwnerToken = 0;
    vector3 derivedPosition;
    bool hasDerivedPosition = false;

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

    //获取本地位置；始终返回作者值，派生覆盖不影响它
    const vector3& GetLocalPosition() const;

    //写入局部位置的派生覆盖。owner 非零，且已有持有者时必须与它一致。
    //返回是否被接受；静态约束在模拟期间拒绝时会返回 false。
    ORBEDEN_BIND_IGNORE
    bool SetDerivedLocalPosition(uint64 owner, const vector3& value);

    //清除派生覆盖，恢复作者位置；只有当前持有者能清除。
    ORBEDEN_BIND_IGNORE
    void ClearDerivedLocalPosition(uint64 owner);

    //判断当前是否存在派生覆盖
    ORBEDEN_BIND_IGNORE
    bool HasDerivedLocalPosition() const;

    //获取解析后的本地位置：有覆盖时用覆盖值，否则用作者值。矩阵计算读这一个。
    ORBEDEN_BIND_IGNORE
    const vector3& GetResolvedLocalPosition() const;

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
