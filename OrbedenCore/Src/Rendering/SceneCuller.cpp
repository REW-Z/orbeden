#include "Rendering/SceneCuller.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Camera.h"

void SceneCuller::Cull(const RenderScene& scene, const RenderCamera& camera, VisibleSet& visibleSet)
{
    visibleSet.Clear();
    visibleSet.camera = camera;

    uint32 layerMask = camera.camera ? camera.camera->drawLayerMask : 0xFFFFFFFFu;
    for (const RenderItem& item : scene.items)
    {
        if ((item.drawLayer & layerMask) == 0) continue;
        if (!RenderMath::Intersects(camera.viewFrustum, item.worldBounds)) continue;

        RenderItem visibleItem = item;
        vector3 toItem =
        {
            item.worldPosition.x - camera.position.x,
            item.worldPosition.y - camera.position.y,
            item.worldPosition.z - camera.position.z,
        };
        visibleItem.cameraDistance = RenderMath::Dot(toItem, toItem);
        visibleSet.items.push_back(visibleItem);
    }
}
