#include "Rendering/SpaceCache.h"

#include "Rendering/RenderMath.h"
#include "Runtime/SpaceComponent.h"

namespace
{
    uint64 MakeKey(EnsId ens)
    {
        return (static_cast<uint64>(ens.id) << 32) | static_cast<uint64>(ens.version);
    }
}

void SpaceCache::Update(World& currentWorld)
{
    world = &currentWorld;
    worldMatrices.clear();

    currentWorld.ForEachEns([this](Ens ens)
    {
        BuildWorldMatrix(ens.GetEns());
    });
}

matrix4x4 SpaceCache::GetWorldMatrix(EnsId ens) const
{
    auto it = worldMatrices.find(MakeKey(ens));
    return it == worldMatrices.end() ? matrix4x4() : it->second;
}

matrix4x4 SpaceCache::BuildWorldMatrix(EnsId ens)
{
    if (!world) return matrix4x4();

    uint64 key = MakeKey(ens);
    auto it = worldMatrices.find(key);
    if (it != worldMatrices.end()) return it->second;

    SpaceComponent* space = world->GetSpaceComponent(ens);
    if (!space) return matrix4x4();

    matrix4x4 local = RenderMath::TRS(space->localPosition, space->localRotation, space->localScale);
    matrix4x4 result = local;
    if (!space->parent.IsNull())
    {
        result = RenderMath::Mul(BuildWorldMatrix(space->parent), local);
    }

    worldMatrices[key] = result;
    return result;
}

