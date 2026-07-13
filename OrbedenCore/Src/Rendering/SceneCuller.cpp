#include "Rendering/SceneCuller.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"

void SceneCuller::Cull(const RenderScene& scene, const RenderCamera& camera, VisibleSet& visibleSet)
{
    //复用可见集合容器，并保存本次剔除对应的相机数据。
    visibleSet.Clear();
    visibleSet.camera = camera;

    //先按绘制层过滤，再按相机视锥过滤场景项。
    uint32 layerMask = camera.drawLayerMask;
    for (usize itemIndex = 0; itemIndex < scene.items.size(); ++itemIndex)
    {
        const RenderItem& item = scene.items[itemIndex];
        if ((item.drawLayer & layerMask) == 0) continue;
        if (!RenderMath::Intersects(camera.viewFrustum, item.worldBounds)) continue;

        //使用对象中心到相机的平方距离，避免排序时进行开方。
        vector3 toItem =
        {
            item.worldPosition.x - camera.position.x,
            item.worldPosition.y - camera.position.y,
            item.worldPosition.z - camera.position.z,
        };

        //记录原始场景索引和距离，供排序器和 Forward 管线继续使用。
        VisibleItem visibleItem;
        visibleItem.itemIndex = static_cast<uint32>(itemIndex);
        visibleItem.cameraDistance = RenderMath::Dot(toItem, toItem);
        visibleSet.items.push_back(visibleItem);
    }
}
