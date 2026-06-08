#include "Rendering/RenderItemSorter.h"

#include <algorithm>

void RenderItemSorter::Sort(VisibleSet& visibleSet)
{
    std::sort(visibleSet.items.begin(), visibleSet.items.end(), [](const RenderItem& a, const RenderItem& b)
    {
        if (a.drawQueue != b.drawQueue) return static_cast<uint32>(a.drawQueue) < static_cast<uint32>(b.drawQueue);
        if (a.drawQueue == DrawQueue::Transparent && a.cameraDistance != b.cameraDistance) return a.cameraDistance > b.cameraDistance;
        if (a.drawQueue == DrawQueue::Opaque && a.cameraDistance != b.cameraDistance) return a.cameraDistance < b.cameraDistance;
        if (a.material != b.material) return a.material < b.material;
        if (a.mesh != b.mesh) return a.mesh < b.mesh;
        return a.subMeshIndex < b.subMeshIndex;
    });
}

