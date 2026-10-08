#include "Runtime/Object/Transform.h"

#include "Runtime/World.h"

OBJECT_TYPE_IMPLEMENT(Transform, Component)

//获取本地位置
const vector3& Transform::GetLocalPosition() const
{
    return localPosition;
}

//写入局部位置的派生覆盖
bool Transform::SetDerivedLocalPosition(uint64 owner, const vector3& value)
{
    if (owner == 0) return false;
    //已经有别的持有者时不抢占，配置冲突留给上层报告。
    if (hasDerivedPosition && derivedOwnerToken != owner) return false;

    //模拟期间 static 的世界变换不变：与作者 setter 一样直接拒绝。
    World* world = GetWorld();
    if (world && !world->CanChangeTransform(GetEnsId())) return false;

    bool unchanged = hasDerivedPosition
        && derivedPosition.x == value.x && derivedPosition.y == value.y && derivedPosition.z == value.z;
    derivedOwnerToken = owner;
    hasDerivedPosition = true;
    if (unchanged) return true;

    derivedPosition = value;
    if (world) world->NotifyTransformChanged(GetEnsId(), TransformChangeSource::Derived);
    return true;
}

//清除派生覆盖，恢复作者位置
void Transform::ClearDerivedLocalPosition(uint64 owner)
{
    if (!hasDerivedPosition || owner == 0 || derivedOwnerToken != owner) return;

    hasDerivedPosition = false;
    derivedOwnerToken = 0;
    //覆盖期间世界矩阵用的是覆盖值，清除后要重算回作者位置。
    if (World* world = GetWorld()) world->NotifyTransformChanged(GetEnsId(), TransformChangeSource::Derived);
}

//判断当前是否存在派生覆盖
bool Transform::HasDerivedLocalPosition() const
{
    return hasDerivedPosition;
}

//获取解析后的本地位置
const vector3& Transform::GetResolvedLocalPosition() const
{
    return hasDerivedPosition ? derivedPosition : localPosition;
}

//设置本地位置并通知变换缓存
void Transform::SetLocalPosition(const vector3& value)
{
    if (localPosition.x == value.x && localPosition.y == value.y && localPosition.z == value.z) return;

    //模拟期间 static 的世界变换不变：保留原值，也不发变换通知
    World* world = GetWorld();
    if (world && !world->CanChangeTransform(GetEnsId())) return;

    //同步作者位移到当前派生位置
    if (hasDerivedPosition)
    {
        derivedPosition.x += value.x - localPosition.x;
        derivedPosition.y += value.y - localPosition.y;
        derivedPosition.z += value.z - localPosition.z;
    }
    localPosition = value;
    if (world) world->NotifyTransformChanged(GetEnsId());
}

//获取本地旋转
const quaternion& Transform::GetLocalRotation() const
{
    return localRotation;
}

//设置本地旋转并通知变换缓存
void Transform::SetLocalRotation(const quaternion& value)
{
    if (localRotation.x == value.x && localRotation.y == value.y &&
        localRotation.z == value.z && localRotation.w == value.w) return;

    World* world = GetWorld();
    if (world && !world->CanChangeTransform(GetEnsId())) return;

    localRotation = value;
    if (world) world->NotifyTransformChanged(GetEnsId());
}

//获取本地缩放
const vector3& Transform::GetLocalScale() const
{
    return localScale;
}

//设置本地缩放并通知变换缓存
void Transform::SetLocalScale(const vector3& value)
{
    if (localScale.x == value.x && localScale.y == value.y && localScale.z == value.z) return;

    World* world = GetWorld();
    if (world && !world->CanChangeTransform(GetEnsId())) return;

    localScale = value;
    if (world) world->NotifyTransformChanged(GetEnsId());
}

EnsId Transform::GetParent() const { return parent; }
void Transform::SetParent(EnsId value)
{
    if (World* world = GetWorld()) world->SetParent(GetEnsId(), value);
}
vector3 Transform::GetWorldPosition() const { return worldPosition; }
quaternion Transform::GetWorldRotation() const { return worldRotation; }
