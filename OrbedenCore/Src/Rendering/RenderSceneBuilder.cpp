#include "Rendering/RenderSceneBuilder.h"

#include "Log/Log.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Object/DirectionalLight.h"
#include "Runtime/Object/StaticMeshRenderer.h"

#include <algorithm>
#include <cmath>

void RenderSceneBuilder::Build(World& world, SpaceCache& spaceCache, int32 viewportWidth, int32 viewportHeight, RenderScene& scene)
{
    scene.Clear();
    scene.renderSettings = world.renderSettings;
    spaceCache.Update(world);

    world.ForEachComponent<Camera>([&](Camera* camera)
    {
        if (!camera->enabled) return;

        RenderCamera renderCamera;
        renderCamera.ens = camera->GetEnsId();
        renderCamera.camera = camera;
        renderCamera.worldMatrix = spaceCache.GetWorldMatrix(renderCamera.ens);
        renderCamera.viewMatrix = RenderMath::Inverse(renderCamera.worldMatrix);
        renderCamera.fieldOfView = camera->fieldOfView;
        renderCamera.nearPlane = camera->nearPlane;
        renderCamera.farPlane = camera->farPlane;
        renderCamera.drawLayerMask = camera->drawLayerMask;
        renderCamera.clearMode = camera->clearMode;
        renderCamera.clearColor = camera->clearColor;
        renderCamera.renderTargetId = { camera->renderTargetId };
        renderCamera.normalizedViewportX = std::clamp(camera->viewportX, 0.0f, 1.0f);
        renderCamera.normalizedViewportY = std::clamp(camera->viewportY, 0.0f, 1.0f);
        renderCamera.normalizedViewportWidth = std::clamp(camera->viewportWidth, 0.0f, 1.0f - renderCamera.normalizedViewportX);
        renderCamera.normalizedViewportHeight = std::clamp(camera->viewportHeight, 0.0f, 1.0f - renderCamera.normalizedViewportY);
        renderCamera.viewportX = static_cast<int32>(renderCamera.normalizedViewportX * static_cast<float32>(viewportWidth));
        renderCamera.viewportY = static_cast<int32>(renderCamera.normalizedViewportY * static_cast<float32>(viewportHeight));
        renderCamera.viewportWidth = std::max(0, static_cast<int32>(renderCamera.normalizedViewportWidth * static_cast<float32>(viewportWidth)));
        renderCamera.viewportHeight = std::max(0, static_cast<int32>(renderCamera.normalizedViewportHeight * static_cast<float32>(viewportHeight)));
        float32 aspect = renderCamera.viewportHeight > 0
            ? static_cast<float32>(renderCamera.viewportWidth) / static_cast<float32>(renderCamera.viewportHeight)
            : 1.0f;
        renderCamera.projectionMatrix = RenderMath::Perspective(renderCamera.fieldOfView, aspect, renderCamera.nearPlane, renderCamera.farPlane);
        renderCamera.viewProjectionMatrix = RenderMath::Mul(renderCamera.projectionMatrix, renderCamera.viewMatrix);
        renderCamera.viewFrustum = RenderMath::BuildFrustum(renderCamera.viewProjectionMatrix);
        renderCamera.position = RenderMath::GetTranslation(renderCamera.worldMatrix);
        renderCamera.depth = camera->depth;
        scene.cameras.push_back(renderCamera);
    });

    std::sort(scene.cameras.begin(), scene.cameras.end(), [](const RenderCamera& a, const RenderCamera& b)
    {
        float32 depthA = std::isfinite(a.depth) ? a.depth : 0.0f;
        float32 depthB = std::isfinite(b.depth) ? b.depth : 0.0f;
        if (depthA != depthB) return depthA < depthB;
        if (a.ens.id != b.ens.id) return a.ens.id < b.ens.id;
        return a.ens.version < b.ens.version;
    });

    world.ForEachComponent<DirectionalLight>([&](DirectionalLight* light)
    {
        if (!light->enabled) return;

        RenderDirectionalLight renderLight;
        renderLight.ens = light->GetEnsId();
        renderLight.light = light;
        renderLight.direction = RenderMath::Normalize(light->direction);
        if (RenderMath::Dot(renderLight.direction, renderLight.direction) <= 0.000001f)
        {
            renderLight.direction = { -0.35f, -1.0f, -0.45f };
        }
        renderLight.color = light->color;
        renderLight.intensity = light->intensity;
        renderLight.castShadows = light->castShadows;
        renderLight.shadowBias = light->shadowBias;
        renderLight.shadowStrength = light->shadowStrength;
        renderLight.shadowDistance = light->shadowDistance;
        scene.directionalLights.push_back(renderLight);
    });

    world.ForEachComponent<StaticMeshRenderer>([&](StaticMeshRenderer* renderer)
    {
        if (!renderer->enabled) return;

        Mesh* mesh = renderer->mesh.Get();
        if (!mesh)
        {
            Log::Error("StaticMeshRenderer render skipped: mesh is missing.");
            return;
        }

        const bounds3& localBounds = mesh->GetLocalBounds();
        if (!localBounds.valid || mesh->indices.empty())
        {
            Log::Error("StaticMeshRenderer render skipped: mesh has no vertices or indices.");
            return;
        }

        matrix4x4 localToWorld = spaceCache.GetWorldMatrix(renderer->GetEnsId());
        bounds3 worldBounds = RenderMath::TransformBounds(localToWorld, localBounds);
        for (uint32 index = 0; index < mesh->subMeshes.size(); ++index)
        {
            const SubMesh& subMesh = mesh->subMeshes[index];
            usize indexStart = static_cast<usize>(subMesh.indexStart);
            usize indexCount = static_cast<usize>(subMesh.indexCount);
            if (indexCount == 0)
            {
                continue;
            }
            if (indexStart > mesh->indices.size() || indexCount > mesh->indices.size() - indexStart)
            {
                Log::Error("StaticMeshRenderer render skipped: submesh index range is invalid.");
                continue;
            }

            Material* material = subMesh.material.Get();
            if (!material)
            {
                Log::Error("StaticMeshRenderer render skipped: submesh material is missing.");
                continue;
            }

            RenderItem item;
            item.ens = renderer->GetEnsId();
            item.renderer = renderer;
            item.mesh = mesh;
            item.material = material;
            item.subMeshIndex = index;
            item.indexStart = subMesh.indexStart;
            item.indexCount = subMesh.indexCount;
            item.drawLayer = renderer->drawLayer;
            item.drawQueue = renderer->drawQueue;
            item.castShadows = renderer->castShadows;
            item.receiveShadows = renderer->receiveShadows;
            item.localToWorld = localToWorld;
            item.localBounds = localBounds;
            item.worldBounds = worldBounds;
            item.worldPosition = worldBounds.center;
            scene.items.push_back(item);
        }
    });
}
