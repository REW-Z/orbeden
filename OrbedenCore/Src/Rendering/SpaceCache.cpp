#include "Rendering/SpaceCache.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/SpaceComponent.h"

#include <algorithm>

namespace
{
    //计算父节点旋转与局部旋转的组合结果
    quaternion Mul(const quaternion& a, const quaternion& b)
    {
        return
        {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        };
    }
}

//销毁缓存并解除世界监听
SpaceCache::~SpaceCache()
{
    if (world) world->RemoveTransformListener(this);
}

//接收空间变换失效通知
void SpaceCache::OnTransformChanged(World& changedWorld, EnsId ens)
{
    if (world != &changedWorld || ens.IsNull()) return;
    if (std::find(pendingNodes.begin(), pendingNodes.end(), ens) != pendingNodes.end()) return;

    pendingNodes.push_back(ens);
}

//处理变更通知并刷新所有受影响节点的世界矩阵
void SpaceCache::Update(World& currentWorld)
{
    if (world != &currentWorld) BindWorld(currentWorld);

    changedNodes.clear();
    if (pendingNodes.empty()) return;

    //只处理没有待处理祖先的根，避免同一子树重复遍历
    List<EnsId> roots;
    for (EnsId ens : pendingNodes)
    {
        if (!currentWorld.IsAlive(ens) || HasPendingAncestor(ens)) continue;
        roots.push_back(ens);
    }
    pendingNodes.clear();

    for (EnsId ens : roots)
    {
        SpaceComponent* space = currentWorld.GetSpaceComponent(ens);
        if (!space) continue;

        CollectChangedRecursive(ens);
        if (!space->transformDirty && space->transformCacheInitialized) continue;

        SpaceComponent* parentSpace = currentWorld.GetSpaceComponent(space->parent);
        matrix4x4 parentMatrix = parentSpace ? parentSpace->worldMatrix : matrix4x4();
        quaternion parentRotation = parentSpace ? parentSpace->worldRotation : quaternion();
        UpdateNodeRecursive(ens, parentMatrix, parentRotation, false);
    }
}

//获取实体的缓存世界矩阵
matrix4x4 SpaceCache::GetWorldMatrix(EnsId ens) const
{
    SpaceComponent* space = world ? world->GetSpaceComponent(ens) : nullptr;
    return space ? space->worldMatrix : matrix4x4();
}

//获取本次更新受影响的节点
const List<EnsId>& SpaceCache::GetChangedNodes() const
{
    return changedNodes;
}

//切换监听世界并建立一次完整缓存
void SpaceCache::BindWorld(World& currentWorld)
{
    if (world) world->RemoveTransformListener(this);

    world = &currentWorld;
    pendingNodes.clear();
    changedNodes.clear();
    world->AddTransformListener(this);

    //初次绑定只收集层级根节点，递归阶段会覆盖完整世界
    currentWorld.ForEachEns([this](Ens& ens)
    {
        SpaceComponent* space = ens.Space();
        if (!space || !space->parent.IsNull()) return;

        space->transformDirty = true;
        pendingNodes.push_back(ens.GetId());
    });
}

//判断节点是否已有待处理祖先
bool SpaceCache::HasPendingAncestor(EnsId ens) const
{
    if (!world) return false;

    SpaceComponent* space = world->GetSpaceComponent(ens);
    EnsId parent = space ? space->parent : EnsId();
    while (!parent.IsNull())
    {
        if (std::find(pendingNodes.begin(), pendingNodes.end(), parent) != pendingNodes.end()) return true;

        SpaceComponent* parentSpace = world->GetSpaceComponent(parent);
        parent = parentSpace ? parentSpace->parent : EnsId();
    }

    return false;
}

//递归收集受影响的空间节点
void SpaceCache::CollectChangedRecursive(EnsId ens)
{
    if (!world) return;

    SpaceComponent* space = world->GetSpaceComponent(ens);
    if (!space) return;

    changedNodes.push_back(ens);
    EnsId child = space->firstChild;
    while (!child.IsNull())
    {
        SpaceComponent* childSpace = world->GetSpaceComponent(child);
        EnsId nextChild = childSpace ? childSpace->next : EnsId();
        CollectChangedRecursive(child);
        child = nextChild;
    }
}

//递归刷新子树世界变换
void SpaceCache::UpdateNodeRecursive(EnsId ens, const matrix4x4& parentMatrix, const quaternion& parentRotation, bool parentDirty)
{
    if (!world) return;

    SpaceComponent* space = world->GetSpaceComponent(ens);
    if (!space) return;

    bool dirty = parentDirty || space->transformDirty || !space->transformCacheInitialized;
    if (dirty)
    {
        const vector3& localPosition = space->GetLocalPosition();
        const quaternion& localRotation = space->GetLocalRotation();
        const vector3& localScale = space->GetLocalScale();
        space->localMatrix = RenderMath::TRS(localPosition, localRotation, localScale);
        space->worldMatrix = space->parent.IsNull() ? space->localMatrix : RenderMath::Mul(parentMatrix, space->localMatrix);
        space->worldRotation = space->parent.IsNull() ? localRotation : Mul(parentRotation, localRotation);
        space->worldPosition = RenderMath::GetTranslation(space->worldMatrix);
        space->transformCacheInitialized = true;
        space->transformDirty = false;
    }

    EnsId child = space->firstChild;
    while (!child.IsNull())
    {
        SpaceComponent* childSpace = world->GetSpaceComponent(child);
        EnsId nextChild = childSpace ? childSpace->next : EnsId();
        UpdateNodeRecursive(child, space->worldMatrix, space->worldRotation, dirty);
        child = nextChild;
    }
}
