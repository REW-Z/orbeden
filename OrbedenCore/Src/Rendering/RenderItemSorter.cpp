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

        if (itemA.drawQueue == DrawQueue::Transparent)
        {
            float32 distanceA = GetSortableDistance(itemA.cameraDistance);
            float32 distanceB = GetSortableDistance(itemB.cameraDistance);
            if (distanceA != distanceB) return distanceA > distanceB;
            if (itemA.renderer != itemB.renderer) return std::less<StaticMeshRenderer*>()(itemA.renderer, itemB.renderer);
            return itemA.subMeshIndex < itemB.subMeshIndex;
        }

        if (itemA.material != itemB.material) return std::less<Material*>()(itemA.material, itemB.material);
        if (itemA.mesh != itemB.mesh) return std::less<Mesh*>()(itemA.mesh, itemB.mesh);
        float32 distanceA = GetSortableDistance(itemA.cameraDistance);
        float32 distanceB = GetSortableDistance(itemB.cameraDistance);
        if (distanceA != distanceB) return distanceA < distanceB;
        if (itemA.renderer != itemB.renderer) return std::less<StaticMeshRenderer*>()(itemA.renderer, itemB.renderer);
        return itemA.subMeshIndex < itemB.subMeshIndex;
    });
}
