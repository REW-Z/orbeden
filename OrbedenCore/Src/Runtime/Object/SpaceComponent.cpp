#include "Runtime/Object/SpaceComponent.h"

#include "Runtime/World.h"

OBJECT_TYPE_IMPLEMENT(SpaceComponent, Component)

//获取本地位置
const vector3& SpaceComponent::GetLocalPosition() const
{
    return localPosition;
}

//设置本地位置并通知空间缓存
void SpaceComponent::SetLocalPosition(const vector3& value)
{
    if (localPosition.x == value.x && localPosition.y == value.y && localPosition.z == value.z) return;

    localPosition = value;
    World* world = GetWorld();
    if (world) world->NotifyTransformChanged(GetEnsId());
}

//获取本地旋转
const quaternion& SpaceComponent::GetLocalRotation() const
{
    return localRotation;
}

//设置本地旋转并通知空间缓存
void SpaceComponent::SetLocalRotation(const quaternion& value)
{
    if (localRotation.x == value.x && localRotation.y == value.y &&
        localRotation.z == value.z && localRotation.w == value.w) return;

    localRotation = value;
    World* world = GetWorld();
    if (world) world->NotifyTransformChanged(GetEnsId());
}

//获取本地缩放
const vector3& SpaceComponent::GetLocalScale() const
{
    return localScale;
}

//设置本地缩放并通知空间缓存
void SpaceComponent::SetLocalScale(const vector3& value)
{
    if (localScale.x == value.x && localScale.y == value.y && localScale.z == value.z) return;

    localScale = value;
    World* world = GetWorld();
    if (world) world->NotifyTransformChanged(GetEnsId());
}
