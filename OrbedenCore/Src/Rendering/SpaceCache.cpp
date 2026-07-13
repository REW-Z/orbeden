#include "Rendering/SpaceCache.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/SpaceComponent.h"

namespace
{
    //比较两个向量的三个分量是否完全一致。
    bool Equal(const vector3& a, const vector3& b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    //比较两个四元数的四个分量是否完全一致。
    bool Equal(const quaternion& a, const quaternion& b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    }

    //计算父节点旋转与局部旋转的组合结果。
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

void SpaceCache::Update(World& currentWorld)
{
    //切换世界时强制所有空间节点重新建立缓存。
    bool worldChanged = world != &currentWorld;
    world = &currentWorld;

    //重新收集本次需要刷新世界矩阵的根节点。
    dirtyNodes.clear();
    DetectTransformChanges(currentWorld, worldChanged);
    if (dirtyNodes.empty()) return;

    //从父节点已经稳定的脏根开始递归更新，避免重复处理子树。
    for (EnsId ens : dirtyNodes)
    {
        SpaceComponent* space = currentWorld.GetSpaceComponent(ens);
        if (!space || (!space->transformDirty && space->transformCacheInitialized)) continue;

        SpaceComponent* parentSpace = space->parent.IsNull() ? nullptr : currentWorld.GetSpaceComponent(space->parent);
        if (parentSpace && (parentSpace->transformDirty || !parentSpace->transformCacheInitialized)) continue;

        //根节点使用单位父变换，子节点使用已缓存的父世界变换。
        matrix4x4 parentMatrix = parentSpace ? parentSpace->worldMatrix : matrix4x4();
        quaternion parentRotation = parentSpace ? parentSpace->worldRotation : quaternion();
        UpdateNodeRecursive(ens, parentMatrix, parentRotation, false);
    }
}

matrix4x4 SpaceCache::GetWorldMatrix(EnsId ens) const
{
    //查询不到世界或实体时返回默认矩阵，调用方可继续处理无效对象。
    SpaceComponent* space = world ? world->GetSpaceComponent(ens) : nullptr;
    return space ? space->worldMatrix : matrix4x4();
}

//检测 public local 字段变更并标记脏状态
void SpaceCache::DetectTransformChanges(World& currentWorld, bool forceUpdate)
{
    //遍历所有实体，检查局部变换、父节点和缓存初始化状态。
    currentWorld.ForEachEns([this, &currentWorld, forceUpdate](Ens& ens)
    {
        SpaceComponent* space = ens.Space();
        if (!space) return;

        //比较缓存快照与当前 public local 字段，判断节点是否需要更新。
        bool changed = forceUpdate ||
            !space->transformCacheInitialized ||
            space->cachedParent != space->parent ||
            !Equal(space->cachedLocalPosition, space->localPosition) ||
            !Equal(space->cachedLocalRotation, space->localRotation) ||
            !Equal(space->cachedLocalScale, space->localScale);

        //局部节点变化会使整棵子树失效；切换世界则直接标记当前节点。
        if (forceUpdate)
        {
            space->transformDirty = true;
        }
        else if (changed && !space->transformDirty)
        {
            MarkDirtyRecursive(ens.GetId());
        }

        //只有父节点已稳定的节点才作为递归更新入口加入脏根列表。
        bool needsUpdate = changed || space->transformDirty || !space->transformCacheInitialized;
        if (!needsUpdate) return;

        SpaceComponent* parentSpace = space->parent.IsNull() ? nullptr : currentWorld.GetSpaceComponent(space->parent);
        if (!parentSpace || (!parentSpace->transformDirty && parentSpace->transformCacheInitialized))
        {
            dirtyNodes.push_back(ens.GetId());
        }
    });
}

//递归标记子树脏状态
void SpaceCache::MarkDirtyRecursive(EnsId ens)
{
    //没有绑定世界或节点已不存在时停止递归。
    if (!world) return;

    SpaceComponent* space = world->GetSpaceComponent(ens);
    if (!space) return;

    //当前节点失效后，所有后代都必须重新组合父世界变换。
    space->transformDirty = true;

    //保存下一个兄弟节点，再递归处理当前子树。
    EnsId child = space->firstChild;
    while (!child.IsNull())
    {
        SpaceComponent* childSpace = world->GetSpaceComponent(child);
        EnsId nextChild = childSpace ? childSpace->next : EnsId();
        MarkDirtyRecursive(child);
        child = nextChild;
    }
}

//递归刷新子树世界变换
void SpaceCache::UpdateNodeRecursive(EnsId ens, const matrix4x4& parentMatrix, const quaternion& parentRotation, bool parentDirty)
{
    //递归入口依赖当前缓存绑定的世界。
    if (!world) return;

    SpaceComponent* space = world->GetSpaceComponent(ens);
    if (!space) return;

    //父节点变化、局部变化或缓存未初始化都会触发重新计算。
    bool dirty = parentDirty || space->transformDirty || !space->transformCacheInitialized;
    if (!dirty) return;

    //先计算局部矩阵，再结合父节点得到世界矩阵和世界旋转。
    space->localMatrix = RenderMath::TRS(space->localPosition, space->localRotation, space->localScale);
    space->worldMatrix = space->parent.IsNull() ? space->localMatrix : RenderMath::Mul(parentMatrix, space->localMatrix);
    space->worldRotation = space->parent.IsNull() ? space->localRotation : Mul(parentRotation, space->localRotation);
    space->worldPosition = RenderMath::GetTranslation(space->worldMatrix);

    //保存当前局部字段快照，并清除节点脏标记。
    space->cachedLocalPosition = space->localPosition;
    space->cachedLocalRotation = space->localRotation;
    space->cachedLocalScale = space->localScale;
    space->cachedParent = space->parent;
    space->transformCacheInitialized = true;
    space->transformDirty = false;

    //无论子节点自身是否变化，父节点更新后都要向下传递脏状态。
    EnsId child = space->firstChild;
    while (!child.IsNull())
    {
        SpaceComponent* childSpace = world->GetSpaceComponent(child);
        EnsId nextChild = childSpace ? childSpace->next : EnsId();
        UpdateNodeRecursive(child, space->worldMatrix, space->worldRotation, true);
        child = nextChild;
    }
}
