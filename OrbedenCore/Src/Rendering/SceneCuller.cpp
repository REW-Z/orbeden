#include "Rendering/SceneCuller.h"

#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Object/StaticMeshRenderer.h"

void SceneCuller::Cull(const RenderScene& scene, const RenderCamera& camera, VisibleSet& visibleSet)
{
    //重置相机可见集合
    visibleSet.Clear();
    visibleSet.camera = camera;

    //筛选可见渲染器
    uint32 layerMask = camera.drawLayerMask;
    for (StaticMeshRenderer* renderer : scene.renderers)
    {
        if (!renderer || !renderer->IsRenderSceneEligible()) continue;

        const StaticMeshRendererRenderState& state = renderer->renderState;
        if (!state.mesh || !state.worldBounds.valid) continue;
        if ((renderer->drawLayer & layerMask) == 0) continue;
        if (!RenderMath::Intersects(camera.viewFrustum, state.worldBounds)) continue;

        //计算相机距离平方
        vector3 toItem =
        {
            state.worldPosition.x - camera.position.x,
            state.worldPosition.y - camera.position.y,
            state.worldPosition.z - camera.position.z,
        };

        //记录可见渲染器
        VisibleItem visibleItem;
        visibleItem.renderer = renderer;
        visibleItem.cameraDistance = RenderMath::Dot(toItem, toItem);
        visibleSet.visibleItems.push_back(visibleItem);
    }
}
