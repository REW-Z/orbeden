#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/World.h"

//空间矩阵缓存，按脏标记刷新 SpaceComponent 的世界变换
class SpaceCache
{
private:
    World* world = nullptr;
    List<EnsId> dirtyNodes;

public:
    //更新世界矩阵缓存
    void Update(World& currentWorld);

    //获取世界矩阵
    matrix4x4 GetWorldMatrix(EnsId ens) const;

private:
    //检测 public local 字段变更并标记脏状态
    void DetectTransformChanges(World& currentWorld, bool forceUpdate);

    //递归标记子树脏状态
    void MarkDirtyRecursive(EnsId ens);

    //递归刷新子树世界变换
    void UpdateNodeRecursive(EnsId ens, const matrix4x4& parentMatrix, const quaternion& parentRotation, bool parentDirty);
};
