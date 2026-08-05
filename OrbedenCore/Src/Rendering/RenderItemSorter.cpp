#include "Rendering/RenderItemSorter.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
    float32 GetSortableDistance(float32 value)
    {
        return std::isfinite(value) ? value : std::numeric_limits<float32>::max();
    }
}

void RenderItemSorter::Sort(VisibleSet& visibleSet)
{
    std::sort(visibleSet.renderItems.begin(), visibleSet.renderItems.end(), [](const RenderItem& itemA, const RenderItem& itemB)
    {
        if (itemA.drawQueue != itemB.drawQueue)
        {
            return static_cast<uint32>(itemA.drawQueue) < static_cast<uint32>(itemB.drawQueue);
        }

        float32 distanceA = GetSortableDistance(itemA.cameraDistance);
        float32 distanceB = GetSortableDistance(itemB.cameraDistance);
        if (itemA.drawQueue != DrawQueue::Opaque)
        {
            if (distanceA != distanceB) return distanceA > distanceB;
            if (itemA.ens.id != itemB.ens.id) return itemA.ens.id < itemB.ens.id;
            if (itemA.ens.version != itemB.ens.version) return itemA.ens.version < itemB.ens.version;
            return itemA.subMeshIndex < itemB.subMeshIndex;
        }

        //不透明物体优先前到后，材质和网格只作为稳定的次级排序。
        if (distanceA != distanceB) return distanceA < distanceB;
        if (itemA.material != itemB.material) return std::less<Material*>()(itemA.material, itemB.material);
        if (itemA.mesh != itemB.mesh) return std::less<Mesh*>()(itemA.mesh, itemB.mesh);
        if (itemA.ens.id != itemB.ens.id) return itemA.ens.id < itemB.ens.id;
        if (itemA.ens.version != itemB.ens.version) return itemA.ens.version < itemB.ens.version;
        return itemA.subMeshIndex < itemB.subMeshIndex;
    });
}
