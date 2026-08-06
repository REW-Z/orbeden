#include "Rendering/TransformCache.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/TransformComponent.h"

#include <algorithm>

namespace
{
    //计算世界旋转
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
TransformCache::~TransformCache()
{
    if (world) world->RemoveTransformListener(this);
}

//接收变换失效通知
void TransformCache::OnTransformChanged(World& changedWorld, EnsId ens)
{
    if (world != &changedWorld || ens.IsNull()) return;
    if (std::find(pendingNodes.begin(), pendingNodes.end(), ens) != pendingNodes.end()) return;

    pendingNodes.push_back(ens);
}

//处理变更通知并刷新所有受影响节点的世界矩阵
void TransformCache::Update(World& currentWorld)
{
    if (world != &currentWorld) BindWorld(currentWorld);

    changedNodes.clear();
    if (pendingNodes.empty()) return;

    //更新脏变换根节点
    List<EnsId> roots;
    for (EnsId ens : pendingNodes)
    {
        if (!currentWorld.IsAlive(ens) || HasPendingAncestor(ens)) continue;
        roots.push_back(ens);
    }
    pendingNodes.clear();

    for (EnsId ens : roots)
    {
        TransformComponent* transform = currentWorld.GetTransformComponent(ens);
        if (!transform) continue;

        CollectChangedRecursive(ens);
        if (!transform->transformDirty && transform->transformCacheInitialized) continue;

        TransformComponent* parentTransform = currentWorld.GetTransformComponent(transform->parent);
        matrix4x4 parentMatrix = parentTransform ? parentTransform->worldMatrix : matrix4x4();
        quaternion parentRotation = parentTransform ? parentTransform->worldRotation : quaternion();
        UpdateNodeRecursive(ens, parentMatrix, parentRotation, false);
    }
}

//获取实体的缓存世界矩阵
matrix4x4 TransformCache::GetWorldMatrix(EnsId ens) const
{
    TransformComponent* transform = world ? world->GetTransformComponent(ens) : nullptr;
    return transform ? transform->worldMatrix : matrix4x4();
}

//获取本次更新受影响的节点
const List<EnsId>& TransformCache::GetChangedNodes() const
{
    return changedNodes;
}

//切换监听世界并建立一次完整缓存
void TransformCache::BindWorld(World& currentWorld)
{
    if (world) world->RemoveTransformListener(this);

    world = &currentWorld;
    pendingNodes.clear();
    changedNodes.clear();
    world->AddTransformListener(this);

    //收集世界层级根节点
    currentWorld.ForEachEns([this](Ens& ens)
    {
        TransformComponent* transform = ens.Transform();
        if (!transform || !transform->parent.IsNull()) return;

        transform->transformDirty = true;
        pendingNodes.push_back(ens.GetId());
    });
}

//判断节点是否已有待处理祖先
bool TransformCache::HasPendingAncestor(EnsId ens) const
{
    if (!world) return false;

    TransformComponent* transform = world->GetTransformComponent(ens);
    EnsId parent = transform ? transform->parent : EnsId();
    while (!parent.IsNull())
    {
        if (std::find(pendingNodes.begin(), pendingNodes.end(), parent) != pendingNodes.end()) return true;

        TransformComponent* parentTransform = world->GetTransformComponent(parent);
        parent = parentTransform ? parentTransform->parent : EnsId();
    }

    return false;
}

//递归收集受影响的变换节点
void TransformCache::CollectChangedRecursive(EnsId ens)
{
    if (!world) return;

    TransformComponent* transform = world->GetTransformComponent(ens);
    if (!transform) return;

    changedNodes.push_back(ens);
    EnsId child = transform->firstChild;
    while (!child.IsNull())
    {
        TransformComponent* childTransform = world->GetTransformComponent(child);
        EnsId nextChild = childTransform ? childTransform->next : EnsId();
        CollectChangedRecursive(child);
        child = nextChild;
    }
}

//递归刷新子树世界变换
void TransformCache::UpdateNodeRecursive(EnsId ens, const matrix4x4& parentMatrix, const quaternion& parentRotation, bool parentDirty)
{
    if (!world) return;

    TransformComponent* transform = world->GetTransformComponent(ens);
    if (!transform) return;

    bool dirty = parentDirty || transform->transformDirty || !transform->transformCacheInitialized;
    if (dirty)
    {
        const vector3& localPosition = transform->GetLocalPosition();
        const quaternion& localRotation = transform->GetLocalRotation();
        const vector3& localScale = transform->GetLocalScale();
        transform->localMatrix = RenderMath::TRS(localPosition, localRotation, localScale);
        transform->worldMatrix = transform->parent.IsNull() ? transform->localMatrix : RenderMath::Mul(parentMatrix, transform->localMatrix);
        transform->worldRotation = transform->parent.IsNull() ? localRotation : Mul(parentRotation, localRotation);
        transform->worldPosition = RenderMath::GetTranslation(transform->worldMatrix);
        transform->transformCacheInitialized = true;
        transform->transformDirty = false;
    }

    EnsId child = transform->firstChild;
    while (!child.IsNull())
    {
        TransformComponent* childTransform = world->GetTransformComponent(child);
        EnsId nextChild = childTransform ? childTransform->next : EnsId();
        UpdateNodeRecursive(child, transform->worldMatrix, transform->worldRotation, dirty);
        child = nextChild;
    }
}
