#include "Rendering/SceneCuller.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Object/StaticMeshRenderer.h"

void SceneCuller::Cull(const RenderScene& scene, const RenderCamera& camera, VisibleSet& visibleSet)
{
    //复用可见集合容器，并保存本次剔除对应的相机数据。
    visibleSet.Clear();
    visibleSet.camera = camera;

    //先按绘制层过滤，再按相机视锥过滤持久渲染器。
    uint32 layerMask = camera.drawLayerMask;
    for (StaticMeshRenderer* renderer : scene.renderers)
    {
        if (!renderer || !renderer->GetEnabled()) continue;

        const StaticMeshRendererRenderState& state = renderer->renderState;
        if (!state.mesh || !state.worldBounds.valid) continue;
        if ((renderer->drawLayer & layerMask) == 0) continue;
        if (!RenderMath::Intersects(camera.viewFrustum, state.worldBounds)) continue;

        //使用对象中心到相机的平方距离，避免排序时进行开方。
        vector3 toItem =
        {
            state.worldPosition.x - camera.position.x,
            state.worldPosition.y - camera.position.y,
            state.worldPosition.z - camera.position.z,
        };

        //记录渲染器指针和距离，供可见后 SubMesh 展开使用。
        VisibleItem visibleItem;
        visibleItem.renderer = renderer;
        visibleItem.cameraDistance = RenderMath::Dot(toItem, toItem);
        visibleSet.visibleItems.push_back(visibleItem);
    }
}
