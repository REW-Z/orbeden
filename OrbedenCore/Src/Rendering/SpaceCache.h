#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/ITransformListener.h"
#include "Runtime/World.h"

//空间矩阵缓存，按变更通知刷新 SpaceComponent 的世界变换
class SpaceCache : public ITransformListener
{
private:
    //当前缓存绑定的世界。
    World* world = nullptr;

    //等待处理的变换失效根节点
    List<EnsId> pendingNodes;

    //本次更新实际受影响的空间节点
    List<EnsId> changedNodes;

public:
    //销毁缓存并解除世界监听
    ~SpaceCache() override;

    //接收空间变换失效通知
    void OnTransformChanged(World& changedWorld, EnsId ens) override;

    //处理变更通知并刷新所有受影响节点的世界矩阵
    void Update(World& currentWorld);

    //获取实体的缓存世界矩阵
    matrix4x4 GetWorldMatrix(EnsId ens) const;

    //获取本次更新受影响的节点
    const List<EnsId>& GetChangedNodes() const;

private:
    //切换监听世界并建立一次完整缓存
    void BindWorld(World& currentWorld);

    //判断节点是否已有待处理祖先
    bool HasPendingAncestor(EnsId ens) const;

    //递归收集受影响的空间节点
    void CollectChangedRecursive(EnsId ens);

    //结合父节点变换递归刷新指定节点及其子树
    void UpdateNodeRecursive(EnsId ens, const matrix4x4& parentMatrix, const quaternion& parentRotation, bool parentDirty);
};
