#include "Rendering/SpaceCache.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/SpaceComponent.h"

namespace
{
    bool Equal(const vector3& a, const vector3& b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    bool Equal(const quaternion& a, const quaternion& b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    }

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
    world = &currentWorld;
    DetectTransformChanges(currentWorld);

    currentWorld.ForEachEns([this](Ens ens)
    {
        if (ens.GetParent().IsValid()) return;

        UpdateNodeRecursive(ens.GetEns(), matrix4x4(), quaternion(), false);
    });
}

matrix4x4 SpaceCache::GetWorldMatrix(EnsId ens) const
{
    SpaceComponent* space = world ? world->GetSpaceComponent(ens) : nullptr;
    return space ? space->worldMatrix : matrix4x4();
}

//检测 public local 字段变更并标记脏状态
void SpaceCache::DetectTransformChanges(World& currentWorld)
{
    currentWorld.ForEachEns([this](Ens ens)
    {
        SpaceComponent* space = ens.Space();
        if (!space) return;

        bool changed = !space->transformCacheInitialized ||
            space->cachedParent != space->parent ||
            !Equal(space->cachedLocalPosition, space->localPosition) ||
            !Equal(space->cachedLocalRotation, space->localRotation) ||
            !Equal(space->cachedLocalScale, space->localScale);

        if (changed)
        {
            if (!space->transformDirty)
            {
                MarkDirtyRecursive(ens.GetEns());
            }
        }
    });
}

//递归标记子树脏状态
void SpaceCache::MarkDirtyRecursive(EnsId ens)
{
    if (!world) return;

    SpaceComponent* space = world->GetSpaceComponent(ens);
    if (!space) return;

    space->transformDirty = true;

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
    if (!world) return;

    SpaceComponent* space = world->GetSpaceComponent(ens);
    if (!space) return;

    bool dirty = parentDirty || space->transformDirty || !space->transformCacheInitialized;
    if (dirty)
    {
        space->localMatrix = RenderMath::TRS(space->localPosition, space->localRotation, space->localScale);
        space->worldMatrix = space->parent.IsNull() ? space->localMatrix : RenderMath::Mul(parentMatrix, space->localMatrix);
        space->worldRotation = space->parent.IsNull() ? space->localRotation : Mul(parentRotation, space->localRotation);
        space->worldPosition = RenderMath::GetTranslation(space->worldMatrix);

        space->cachedLocalPosition = space->localPosition;
        space->cachedLocalRotation = space->localRotation;
        space->cachedLocalScale = space->localScale;
        space->cachedParent = space->parent;
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
