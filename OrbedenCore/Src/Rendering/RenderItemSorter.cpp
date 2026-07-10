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

void RenderItemSorter::Sort(const RenderScene& scene, VisibleSet& visibleSet)
{
    std::sort(visibleSet.items.begin(), visibleSet.items.end(), [&scene](const VisibleItem& a, const VisibleItem& b)
    {
        const RenderItem& itemA = scene.items[a.itemIndex];
        const RenderItem& itemB = scene.items[b.itemIndex];
        if (itemA.drawQueue != itemB.drawQueue)
        {
            return static_cast<uint32>(itemA.drawQueue) < static_cast<uint32>(itemB.drawQueue);
        }

        if (itemA.drawQueue == DrawQueue::Transparent)
        {
            float32 distanceA = GetSortableDistance(a.cameraDistance);
            float32 distanceB = GetSortableDistance(b.cameraDistance);
            if (distanceA != distanceB) return distanceA > distanceB;
            return a.itemIndex < b.itemIndex;
        }

        if (itemA.material != itemB.material) return std::less<Material*>()(itemA.material, itemB.material);
        if (itemA.mesh != itemB.mesh) return std::less<Mesh*>()(itemA.mesh, itemB.mesh);
        float32 distanceA = GetSortableDistance(a.cameraDistance);
        float32 distanceB = GetSortableDistance(b.cameraDistance);
        if (distanceA != distanceB) return distanceA < distanceB;
        return a.itemIndex < b.itemIndex;
    });
}
