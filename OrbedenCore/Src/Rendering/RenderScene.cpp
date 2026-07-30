#include "Rendering/RenderScene.h"

#include "Rendering/RenderMath.h"
#include "Rendering/TransformCache.h"
#include "Runtime/Ens.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Object/TransformComponent.h"
#include "Runtime/Object/StaticMeshRenderer.h"
#include "Runtime/World.h"

#include <algorithm>
#include <cmath>

namespace
{
    RenderScene* currentRenderScene = nullptr;
}

//清空上一相机留下的临时数据
void VisibleSet::Clear()
{
    camera = RenderCamera();
    visibleItems.clear();
    renderItems.clear();
}

//获取当前活动的持久渲染场景
RenderScene* GetRenderScene()
{
    return currentRenderScene;
}

//分配带版本的场景槽位
RenderSceneHandle RenderScene::AllocateHandle(List<HandleSlot>& slots, List<uint32>& freeSlots)
{
    RenderSceneHandle handle;
    if (!freeSlots.empty())
    {
        handle.id = freeSlots.back();
        freeSlots.pop_back();
        HandleSlot& slot = slots[handle.id];
        slot.version++;
        if (slot.version == 0) slot.version = 1;
        handle.version = slot.version;
        return handle;
    }

    handle.id = static_cast<uint32>(slots.size());
    handle.version = 1;
    slots.push_back({ handle.version, EnsId::InvalidId });
    return handle;
}

//释放场景槽位供后续复用
void RenderScene::ReleaseHandle(RenderSceneHandle handle, List<HandleSlot>& slots, List<uint32>& freeSlots)
{
    if (!handle.IsValid() || handle.id >= slots.size()) return;

    HandleSlot& slot = slots[handle.id];
    if (slot.version != handle.version) return;

    slot.denseIndex = EnsId::InvalidId;
    freeSlots.push_back(handle.id);
}

//绑定世界并完整收集一次已有渲染组件
void RenderScene::BindWorld(World& currentWorld)
{
    if (world == &currentWorld && currentRenderScene == this) return;

    UnbindWorld();
    world = &currentWorld;
    currentRenderScene = this;

    //初次绑定完整收集，后续增删由组件生命周期主动维护
    currentWorld.ForEachComponent<Camera>([this](Camera* camera)
    {
        if (camera->GetEnabled()) camera->renderSceneHandle = RegisterCamera(camera);
    });
    currentWorld.ForEachComponent<DirectionalLight>([this](DirectionalLight* light)
    {
        if (light->GetEnabled()) light->renderSceneHandle = RegisterDirectionalLight(light);
    });
    currentWorld.ForEachComponent<StaticMeshRenderer>([this](StaticMeshRenderer* renderer)
    {
        if (renderer->GetEnabled()) renderer->renderSceneHandle = RegisterRenderer(renderer);
    });
}

//解除世界绑定并清空全部持久记录
void RenderScene::UnbindWorld()
{
    readDepth = 0;
    FlushPendingChanges();

    for (CameraEntry& entry : cameraEntries)
    {
        if (entry.camera) entry.camera->renderSceneHandle = RenderSceneHandle();
    }
    for (DirectionalLightEntry& entry : directionalLightEntries)
    {
        if (entry.light) entry.light->renderSceneHandle = RenderSceneHandle();
    }
    for (RendererEntry& entry : renderers)
    {
        if (entry.renderer) entry.renderer->renderSceneHandle = RenderSceneHandle();
    }

    world = nullptr;
    cameraEntries.clear();
    directionalLightEntries.clear();
    renderers.clear();
    cameras.clear();
    directionalLights.clear();
    cameraSlots.clear();
    directionalLightSlots.clear();
    rendererSlots.clear();
    freeCameraSlots.clear();
    freeDirectionalLightSlots.clear();
    freeRendererSlots.clear();
    pendingChanges.clear();
    renderSettings = RenderSettings();
    if (currentRenderScene == this) currentRenderScene = nullptr;
}

//增量刷新变换状态和组件快照
void RenderScene::Update(World& currentWorld, TransformCache& transformCache)
{
    if (world != &currentWorld) BindWorld(currentWorld);
    FlushPendingChanges();
    renderSettings = currentWorld.renderSettings;
    transformCache.Update(currentWorld);

    //只刷新收到变换通知的渲染器
    for (EnsId ens : transformCache.GetChangedNodes())
    {
        Ens* entity = currentWorld.GetEns(ens);
        StaticMeshRenderer* renderer = entity ? entity->GetComponent<StaticMeshRenderer>() : nullptr;
        if (renderer && renderer->GetEnabled()) UpdateRenderer(renderer->renderSceneHandle, true);
    }

    //Mesh 引用和几何版本可直接修改，因此只做 renderer 级轻量版本检查
    for (RendererEntry& entry : renderers)
    {
        if (!entry.active) continue;
        Mesh* mesh = entry.renderer ? entry.renderer->mesh.Get() : nullptr;
        uint64 revision = mesh ? mesh->GetRevision() : 0;
        if (mesh != entry.mesh || revision != entry.meshRevision)
        {
            UpdateRenderer(entry.handle, false);
        }
    }

    //相机参数可公开修改，每帧仅刷新已注册相机，不再扫描 World
    cameras.clear();
    for (const CameraEntry& entry : cameraEntries)
    {
        Camera* camera = entry.camera;
        if (!camera || !camera->GetEnabled()) continue;

        RenderCamera renderCamera;
        renderCamera.ens = camera->GetEnsId();
        renderCamera.camera = camera;
        renderCamera.worldMatrix = transformCache.GetWorldMatrix(renderCamera.ens);
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
        renderCamera.position = RenderMath::GetTranslation(renderCamera.worldMatrix);
        renderCamera.depth = camera->depth;
        cameras.push_back(renderCamera);
    }
    std::sort(cameras.begin(), cameras.end(), [](const RenderCamera& a, const RenderCamera& b)
    {
        float32 depthA = std::isfinite(a.depth) ? a.depth : 0.0f;
        float32 depthB = std::isfinite(b.depth) ? b.depth : 0.0f;
        if (depthA != depthB) return depthA < depthB;
        if (a.ens.id != b.ens.id) return a.ens.id < b.ens.id;
        return a.ens.version < b.ens.version;
    });

    //方向光同样只刷新已注册组件快照
    directionalLights.clear();
    for (const DirectionalLightEntry& entry : directionalLightEntries)
    {
        DirectionalLight* light = entry.light;
        if (!light || !light->GetEnabled()) continue;

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
        directionalLights.push_back(renderLight);
    }
}

//进入不允许修改紧凑列表的读取阶段
void RenderScene::BeginRead()
{
    readDepth++;
}

//结束读取阶段并应用延迟增删
void RenderScene::EndRead()
{
    if (readDepth == 0) return;

    readDepth--;
    if (readDepth == 0) FlushPendingChanges();
}

//注册启用的相机组件
RenderSceneHandle RenderScene::RegisterCamera(Camera* camera)
{
    if (!camera || !camera->GetEnabled() || camera->GetWorld() != world) return RenderSceneHandle();
    if (camera->renderSceneHandle.IsValid()) return camera->renderSceneHandle;

    RenderSceneHandle handle = AllocateHandle(cameraSlots, freeCameraSlots);
    PendingChange change{ EntryType::Camera, true, handle, camera };
    if (readDepth > 0) pendingChanges.push_back(change);
    else AddCamera(handle, camera);
    return handle;
}

//注销相机组件
void RenderScene::UnregisterCamera(RenderSceneHandle handle)
{
    if (!handle.IsValid()) return;

    if (readDepth > 0)
    {
        if (CancelPendingAdd(EntryType::Camera, handle))
        {
            ReleaseHandle(handle, cameraSlots, freeCameraSlots);
            return;
        }
        pendingChanges.push_back({ EntryType::Camera, false, handle, nullptr });
    }
    else
    {
        RemoveCamera(handle);
    }
}

//注册启用的方向光组件
RenderSceneHandle RenderScene::RegisterDirectionalLight(DirectionalLight* light)
{
    if (!light || !light->GetEnabled() || light->GetWorld() != world) return RenderSceneHandle();
    if (light->renderSceneHandle.IsValid()) return light->renderSceneHandle;

    RenderSceneHandle handle = AllocateHandle(directionalLightSlots, freeDirectionalLightSlots);
    PendingChange change{ EntryType::DirectionalLight, true, handle, light };
    if (readDepth > 0) pendingChanges.push_back(change);
    else AddDirectionalLight(handle, light);
    return handle;
}

//注销方向光组件
void RenderScene::UnregisterDirectionalLight(RenderSceneHandle handle)
{
    if (!handle.IsValid()) return;

    if (readDepth > 0)
    {
        if (CancelPendingAdd(EntryType::DirectionalLight, handle))
        {
            ReleaseHandle(handle, directionalLightSlots, freeDirectionalLightSlots);
            return;
        }
        pendingChanges.push_back({ EntryType::DirectionalLight, false, handle, nullptr });
    }
    else
    {
        RemoveDirectionalLight(handle);
    }
}

//注册启用的静态网格渲染器
RenderSceneHandle RenderScene::RegisterRenderer(StaticMeshRenderer* renderer)
{
    if (!renderer || !renderer->GetEnabled() || renderer->GetWorld() != world) return RenderSceneHandle();
    if (renderer->renderSceneHandle.IsValid()) return renderer->renderSceneHandle;

    RenderSceneHandle handle = AllocateHandle(rendererSlots, freeRendererSlots);
    PendingChange change{ EntryType::Renderer, true, handle, renderer };
    if (readDepth > 0) pendingChanges.push_back(change);
    else AddRenderer(handle, renderer);
    return handle;
}

//注销静态网格渲染器
void RenderScene::UnregisterRenderer(RenderSceneHandle handle)
{
    if (!handle.IsValid()) return;

    if (readDepth > 0)
    {
        if (CancelPendingAdd(EntryType::Renderer, handle))
        {
            ReleaseHandle(handle, rendererSlots, freeRendererSlots);
            return;
        }
        if (handle.id < rendererSlots.size())
        {
            HandleSlot& slot = rendererSlots[handle.id];
            if (slot.version == handle.version && slot.denseIndex < renderers.size())
            {
                renderers[slot.denseIndex].active = false;
            }
        }
        pendingChanges.push_back({ EntryType::Renderer, false, handle, nullptr });
    }
    else
    {
        RemoveRenderer(handle);
    }
}

//把相机可见渲染器展开为临时子网格绘制项
void RenderScene::BuildRenderItems(VisibleSet& visibleSet) const
{
    visibleSet.renderItems.clear();
    for (const VisibleItem& visible : visibleSet.visibleItems)
    {
        if (visible.rendererIndex >= renderers.size()) continue;

        const RendererEntry& entry = renderers[visible.rendererIndex];
        if (!entry.active) continue;
        StaticMeshRenderer* renderer = entry.renderer;
        Mesh* mesh = entry.mesh;
        if (!renderer || !renderer->GetEnabled() || !mesh || !entry.localBounds.valid) continue;

        for (uint32 index = 0; index < mesh->subMeshes.size(); ++index)
        {
            const SubMesh& subMesh = mesh->subMeshes[index];
            usize start = static_cast<usize>(subMesh.indexStart);
            usize count = static_cast<usize>(subMesh.indexCount);
            if (count == 0 || start > mesh->indices.size() || count > mesh->indices.size() - start) continue;

            Material* material = subMesh.material.Get();
            if (!material) continue;

            RenderItem item;
            item.ens = entry.ens;
            item.renderer = renderer;
            item.mesh = mesh;
            item.material = material;
            item.subMeshIndex = index;
            item.indexStart = subMesh.indexStart;
            item.indexCount = subMesh.indexCount;
            item.drawLayer = renderer->drawLayer;
            item.drawQueue = renderer->drawQueue;
            item.cameraDistance = visible.cameraDistance;
            item.localToWorld = entry.localToWorld;
            item.localBounds = entry.localBounds;
            item.worldBounds = entry.worldBounds;
            item.worldPosition = entry.worldPosition;
            item.castShadows = renderer->castShadows;
            item.receiveShadows = renderer->receiveShadows;
            visibleSet.renderItems.push_back(item);
        }
    }
}

//应用等待安全阶段处理的增删操作
void RenderScene::FlushPendingChanges()
{
    if (readDepth > 0 || pendingChanges.empty()) return;

    List<PendingChange> changes;
    changes.swap(pendingChanges);
    for (const PendingChange& change : changes)
    {
        switch (change.type)
        {
        case EntryType::Camera:
            if (change.add) AddCamera(change.handle, static_cast<Camera*>(change.component));
            else RemoveCamera(change.handle);
            break;
        case EntryType::DirectionalLight:
            if (change.add) AddDirectionalLight(change.handle, static_cast<DirectionalLight*>(change.component));
            else RemoveDirectionalLight(change.handle);
            break;
        case EntryType::Renderer:
            if (change.add) AddRenderer(change.handle, static_cast<StaticMeshRenderer*>(change.component));
            else RemoveRenderer(change.handle);
            break;
        }
    }
}

//取消尚未进入紧凑列表的注册
bool RenderScene::CancelPendingAdd(EntryType type, RenderSceneHandle handle)
{
    auto it = std::find_if(pendingChanges.begin(), pendingChanges.end(), [type, handle](const PendingChange& change)
    {
        return change.add && change.type == type && change.handle == handle;
    });
    if (it == pendingChanges.end()) return false;

    pendingChanges.erase(it);
    return true;
}

//激活相机注册
void RenderScene::AddCamera(RenderSceneHandle handle, Camera* camera)
{
    if (!handle.IsValid() || handle.id >= cameraSlots.size() || !camera) return;

    HandleSlot& slot = cameraSlots[handle.id];
    if (slot.version != handle.version || slot.denseIndex != EnsId::InvalidId) return;

    slot.denseIndex = static_cast<uint32>(cameraEntries.size());
    cameraEntries.push_back({ handle, camera });
}

//移除相机注册
void RenderScene::RemoveCamera(RenderSceneHandle handle)
{
    if (!handle.IsValid() || handle.id >= cameraSlots.size()) return;

    HandleSlot& slot = cameraSlots[handle.id];
    if (slot.version != handle.version || slot.denseIndex >= cameraEntries.size()) return;

    uint32 index = slot.denseIndex;
    CameraEntry moved = cameraEntries.back();
    cameraEntries[index] = moved;
    cameraEntries.pop_back();
    if (index < cameraEntries.size()) cameraSlots[moved.handle.id].denseIndex = index;
    ReleaseHandle(handle, cameraSlots, freeCameraSlots);
}

//激活方向光注册
void RenderScene::AddDirectionalLight(RenderSceneHandle handle, DirectionalLight* light)
{
    if (!handle.IsValid() || handle.id >= directionalLightSlots.size() || !light) return;

    HandleSlot& slot = directionalLightSlots[handle.id];
    if (slot.version != handle.version || slot.denseIndex != EnsId::InvalidId) return;

    slot.denseIndex = static_cast<uint32>(directionalLightEntries.size());
    directionalLightEntries.push_back({ handle, light });
}

//移除方向光注册
void RenderScene::RemoveDirectionalLight(RenderSceneHandle handle)
{
    if (!handle.IsValid() || handle.id >= directionalLightSlots.size()) return;

    HandleSlot& slot = directionalLightSlots[handle.id];
    if (slot.version != handle.version || slot.denseIndex >= directionalLightEntries.size()) return;

    uint32 index = slot.denseIndex;
    DirectionalLightEntry moved = directionalLightEntries.back();
    directionalLightEntries[index] = moved;
    directionalLightEntries.pop_back();
    if (index < directionalLightEntries.size()) directionalLightSlots[moved.handle.id].denseIndex = index;
    ReleaseHandle(handle, directionalLightSlots, freeDirectionalLightSlots);
}

//激活渲染器注册
void RenderScene::AddRenderer(RenderSceneHandle handle, StaticMeshRenderer* renderer)
{
    if (!handle.IsValid() || handle.id >= rendererSlots.size() || !renderer) return;

    HandleSlot& slot = rendererSlots[handle.id];
    if (slot.version != handle.version || slot.denseIndex != EnsId::InvalidId) return;

    RendererEntry entry;
    entry.handle = handle;
    entry.ens = renderer->GetEnsId();
    entry.renderer = renderer;
    slot.denseIndex = static_cast<uint32>(renderers.size());
    renderers.push_back(entry);
    UpdateRenderer(handle, true);
}

//移除渲染器注册
void RenderScene::RemoveRenderer(RenderSceneHandle handle)
{
    if (!handle.IsValid() || handle.id >= rendererSlots.size()) return;

    HandleSlot& slot = rendererSlots[handle.id];
    if (slot.version != handle.version || slot.denseIndex >= renderers.size()) return;

    uint32 index = slot.denseIndex;
    RendererEntry moved = renderers.back();
    renderers[index] = moved;
    renderers.pop_back();
    if (index < renderers.size()) rendererSlots[moved.handle.id].denseIndex = index;
    ReleaseHandle(handle, rendererSlots, freeRendererSlots);
}

//刷新指定渲染器的变换和包围盒
void RenderScene::UpdateRenderer(RenderSceneHandle handle, bool updateTransform)
{
    if (!world || !handle.IsValid() || handle.id >= rendererSlots.size()) return;

    HandleSlot& slot = rendererSlots[handle.id];
    if (slot.version != handle.version || slot.denseIndex >= renderers.size()) return;

    RendererEntry& entry = renderers[slot.denseIndex];
    Mesh* mesh = entry.renderer ? entry.renderer->mesh.Get() : nullptr;
    bool meshChanged = mesh != entry.mesh || (mesh && mesh->GetRevision() != entry.meshRevision);
    if (meshChanged)
    {
        entry.mesh = mesh;
        entry.meshRevision = mesh ? mesh->GetRevision() : 0;
        entry.localBounds = mesh ? mesh->GetLocalBounds() : bounds3();
    }

    if (updateTransform)
    {
        TransformComponent* transform = world->GetTransformComponent(entry.ens);
        entry.localToWorld = transform ? transform->worldMatrix : matrix4x4();
    }

    if (meshChanged || updateTransform)
    {
        entry.worldBounds = entry.localBounds.valid
            ? RenderMath::TransformBounds(entry.localToWorld, entry.localBounds)
            : bounds3();
        entry.worldPosition = entry.worldBounds.center;
    }
}
