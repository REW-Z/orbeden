#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/World.h"

//空间矩阵缓存，按脏标记刷新 SpaceComponent 的世界变换，供场景构建阶段复用。
class SpaceCache
{
private:
    //当前缓存绑定的世界。
    World* world = nullptr;

    //本帧检测到变换变化，需要重新计算的根节点列表。
    List<EnsId> dirtyNodes;

public:
    //检测变换变化并刷新所有受影响节点的世界矩阵。
    void Update(World& currentWorld);

    //获取实体的缓存世界矩阵。
    matrix4x4 GetWorldMatrix(EnsId ens) const;

private:
    //检测 public local 字段变更，并将受影响的层级标记为脏。
    void DetectTransformChanges(World& currentWorld, bool forceUpdate);

    //递归标记指定节点及其子树为脏状态。
    void MarkDirtyRecursive(EnsId ens);

    //结合父节点变换，递归刷新指定节点及其子树的世界变换。
    void UpdateNodeRecursive(EnsId ens, const matrix4x4& parentMatrix, const quaternion& parentRotation, bool parentDirty);
};
