#include "Rendering/SceneCuller.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"

void SceneCuller::Cull(const RenderScene& scene, const RenderCamera& camera, VisibleSet& visibleSet)
{
    visibleSet.Clear();
    visibleSet.camera = camera;

    uint32 layerMask = camera.drawLayerMask;
    for (usize itemIndex = 0; itemIndex < scene.items.size(); ++itemIndex)
    {
        const RenderItem& item = scene.items[itemIndex];
        if ((item.drawLayer & layerMask) == 0) continue;
        if (!RenderMath::Intersects(camera.viewFrustum, item.worldBounds)) continue;

        vector3 toItem =
        {
            item.worldPosition.x - camera.position.x,
            item.worldPosition.y - camera.position.y,
            item.worldPosition.z - camera.position.z,
        };

        VisibleItem visibleItem;
        visibleItem.itemIndex = static_cast<uint32>(itemIndex);
        visibleItem.cameraDistance = RenderMath::Dot(toItem, toItem);
        visibleSet.items.push_back(visibleItem);
    }
}
