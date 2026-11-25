#include "Rendering/SceneCuller.h"

#include "Rendering/Math.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Object/StaticMeshRenderer.h"

void SceneCuller::Cull(const RenderScene& scene, const RenderCamera& camera, VisibleSet& visibleSet)
{
    //重置相机可见集合
    visibleSet.Clear();
    visibleSet.camera = camera;

    //记录可见渲染器及相机距离平方
    auto addVisible = [&camera, &visibleSet](StaticMeshRenderer* renderer)
    {
        const StaticMeshRendererRenderState& state = renderer->renderState;
        vector3 toItem =
        {
            state.worldPosition.x - camera.position.x,
            state.worldPosition.y - camera.position.y,
            state.worldPosition.z - camera.position.z,
        };

        //记录可见渲染器
        VisibleItem visibleItem;
        visibleItem.renderer = renderer;
        visibleItem.cameraDistance = Orbeden::Math::Dot(toItem, toItem);
        visibleSet.visibleItems.push_back(visibleItem);
    };

    //每四个候选渲染器检测一次视锥
    StaticMeshRenderer* candidates[4];
    const bounds3* bounds[4];
    uint32 count = 0;
    uint32 layerMask = camera.drawLayerMask;
    for (StaticMeshRenderer* renderer : scene.renderers)
    {
        if (!renderer || !renderer->IsRenderSceneEligible()) continue;

        const StaticMeshRendererRenderState& state = renderer->renderState;
        if (!state.mesh || !state.worldBounds.valid) continue;
        if ((state.drawLayer & layerMask) == 0) continue;

        candidates[count] = renderer;
        bounds[count] = &state.worldBounds;
        ++count;
        if (count != 4)
        {
            continue;
        }

        uint32 visible = Orbeden::Math::IntersectsFour(camera.viewFrustum, bounds);
        for (uint32 index = 0; index < count; ++index)
        {
            if ((visible & (1u << index)) != 0)
            {
                addVisible(candidates[index]);
            }
        }
        count = 0;
    }

    //检测末尾不足四个的候选渲染器
    for (uint32 index = 0; index < count; ++index)
    {
        if (Orbeden::Math::Intersects(camera.viewFrustum, *bounds[index]))
        {
            addVisible(candidates[index]);
        }
    }
}
