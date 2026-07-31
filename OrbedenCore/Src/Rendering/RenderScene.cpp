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
        if (camera->GetEnabled()) RegisterCamera(camera);
    });
    currentWorld.ForEachComponent<DirectionalLight>([this](DirectionalLight* light)
    {
        if (light->GetEnabled()) RegisterDirectionalLight(light);
    });
    currentWorld.ForEachComponent<StaticMeshRenderer>([this](StaticMeshRenderer* renderer)
    {
        if (renderer->GetEnabled()) RegisterRenderer(renderer);
    });
}

//解除世界绑定并清空全部持久记录
void RenderScene::UnbindWorld()
{
    readDepth = 0;
    world = nullptr;
    cameraComponents.clear();
    directionalLightComponents.clear();
    renderers.clear();
    cameras.clear();
    directionalLights.clear();
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
        if (renderer && renderer->GetEnabled()) UpdateRenderer(renderer, true);
    }

    //Mesh 引用和几何版本可直接修改，因此只做 renderer 级轻量版本检查
    for (StaticMeshRenderer* renderer : renderers)
    {
        if (!renderer || !renderer->GetEnabled()) continue;

        Mesh* mesh = renderer->mesh.Get();
        uint64 revision = mesh ? mesh->GetRevision() : 0;
        if (mesh != renderer->renderState.mesh || revision != renderer->renderState.meshRevision)
        {
            UpdateRenderer(renderer, false);
        }
    }

    //相机
    cameras.clear();
    for (Camera* camera : cameraComponents)
    {
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

    //方向光
    directionalLights.clear();
    for (DirectionalLight* light : directionalLightComponents)
    {
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

//进入不允许修改组件指针列表的读取阶段
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
void RenderScene::RegisterCamera(Camera* camera)
{
    if (!camera || !camera->GetEnabled() || camera->GetWorld() != world) return;

    if (readDepth > 0) pendingChanges.push_back({ ComponentType::Camera, true, camera });
    else AddCamera(camera);
}

//注销相机组件
void RenderScene::UnregisterCamera(Camera* camera)
{
    if (!camera) return;

    if (readDepth > 0)
    {
        if (CancelPendingAdd(ComponentType::Camera, camera)) return;
        pendingChanges.push_back({ ComponentType::Camera, false, camera });
    }
    else
    {
        RemoveCamera(camera);
    }
}

//注册启用的方向光组件
void RenderScene::RegisterDirectionalLight(DirectionalLight* light)
{
    if (!light || !light->GetEnabled() || light->GetWorld() != world) return;

    if (readDepth > 0) pendingChanges.push_back({ ComponentType::DirectionalLight, true, light });
    else AddDirectionalLight(light);
}

//注销方向光组件
void RenderScene::UnregisterDirectionalLight(DirectionalLight* light)
{
    if (!light) return;

    if (readDepth > 0)
    {
        if (CancelPendingAdd(ComponentType::DirectionalLight, light)) return;
        pendingChanges.push_back({ ComponentType::DirectionalLight, false, light });
    }
    else
    {
        RemoveDirectionalLight(light);
    }
}

//注册启用的静态网格渲染器
void RenderScene::RegisterRenderer(StaticMeshRenderer* renderer)
{
    if (!renderer || !renderer->GetEnabled() || renderer->GetWorld() != world) return;

    if (readDepth > 0) pendingChanges.push_back({ ComponentType::Renderer, true, renderer });
    else AddRenderer(renderer);
}

//注销静态网格渲染器
void RenderScene::UnregisterRenderer(StaticMeshRenderer* renderer)
{
    if (!renderer) return;

    if (readDepth > 0)
    {
        if (CancelPendingAdd(ComponentType::Renderer, renderer)) return;
        pendingChanges.push_back({ ComponentType::Renderer, false, renderer });
    }
    else
    {
        RemoveRenderer(renderer);
    }
}

//把相机可见渲染器展开为临时子网格绘制项
void RenderScene::BuildRenderItems(VisibleSet& visibleSet) const
{
    visibleSet.renderItems.clear();
    for (const VisibleItem& visible : visibleSet.visibleItems)
    {
        StaticMeshRenderer* renderer = visible.renderer;
        if (!renderer || !renderer->GetEnabled()) continue;

        const StaticMeshRendererRenderState& state = renderer->renderState;
        Mesh* mesh = state.mesh;
        if (!mesh || !state.localBounds.valid) continue;

        for (uint32 index = 0; index < mesh->subMeshes.size(); ++index)
        {
            const SubMesh& subMesh = mesh->subMeshes[index];
            usize start = static_cast<usize>(subMesh.indexStart);
            usize count = static_cast<usize>(subMesh.indexCount);
            if (count == 0 || start > mesh->indices.size() || count > mesh->indices.size() - start) continue;

            Material* material = subMesh.material.Get();
            if (!material) continue;

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
            item.cameraDistance = visible.cameraDistance;
            item.localToWorld = state.localToWorld;
            item.localBounds = state.localBounds;
            item.worldBounds = state.worldBounds;
            item.worldPosition = state.worldPosition;
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
        case ComponentType::Camera:
            if (change.add) AddCamera(static_cast<Camera*>(change.component));
            else RemoveCamera(static_cast<Camera*>(change.component));
            break;
        case ComponentType::DirectionalLight:
            if (change.add) AddDirectionalLight(static_cast<DirectionalLight*>(change.component));
            else RemoveDirectionalLight(static_cast<DirectionalLight*>(change.component));
            break;
        case ComponentType::Renderer:
            if (change.add) AddRenderer(static_cast<StaticMeshRenderer*>(change.component));
            else RemoveRenderer(static_cast<StaticMeshRenderer*>(change.component));
            break;
        }
    }
}

//取消尚未进入指针列表的注册
bool RenderScene::CancelPendingAdd(ComponentType type, Component* component)
{
    auto newEnd = std::remove_if(pendingChanges.begin(), pendingChanges.end(), [type, component](const PendingChange& change)
    {
        return change.add && change.type == type && change.component == component;
    });
    bool canceled = newEnd != pendingChanges.end();
    pendingChanges.erase(newEnd, pendingChanges.end());
    return canceled;
}

//激活相机注册
void RenderScene::AddCamera(Camera* camera)
{
    if (!camera || std::find(cameraComponents.begin(), cameraComponents.end(), camera) != cameraComponents.end()) return;
    cameraComponents.push_back(camera);
}

//移除相机注册
void RenderScene::RemoveCamera(Camera* camera)
{
    auto it = std::find(cameraComponents.begin(), cameraComponents.end(), camera);
    if (it == cameraComponents.end()) return;

    *it = cameraComponents.back();
    cameraComponents.pop_back();
}

//激活方向光注册
void RenderScene::AddDirectionalLight(DirectionalLight* light)
{
    if (!light || std::find(directionalLightComponents.begin(), directionalLightComponents.end(), light) != directionalLightComponents.end()) return;
    directionalLightComponents.push_back(light);
}

//移除方向光注册
void RenderScene::RemoveDirectionalLight(DirectionalLight* light)
{
    auto it = std::find(directionalLightComponents.begin(), directionalLightComponents.end(), light);
    if (it == directionalLightComponents.end()) return;

    *it = directionalLightComponents.back();
    directionalLightComponents.pop_back();
}

//激活渲染器注册
void RenderScene::AddRenderer(StaticMeshRenderer* renderer)
{
    if (!renderer || std::find(renderers.begin(), renderers.end(), renderer) != renderers.end()) return;

    renderers.push_back(renderer);
    UpdateRenderer(renderer, true);
}

//移除渲染器注册
void RenderScene::RemoveRenderer(StaticMeshRenderer* renderer)
{
    auto it = std::find(renderers.begin(), renderers.end(), renderer);
    if (it == renderers.end()) return;

    *it = renderers.back();
    renderers.pop_back();
}

//刷新指定渲染器的变换和包围盒
void RenderScene::UpdateRenderer(StaticMeshRenderer* renderer, bool updateTransform)
{
    if (!world || !renderer || renderer->GetWorld() != world) return;

    StaticMeshRendererRenderState& state = renderer->renderState;
    Mesh* mesh = renderer->mesh.Get();
    bool meshChanged = mesh != state.mesh || (mesh && mesh->GetRevision() != state.meshRevision);
    if (meshChanged)
    {
        state.mesh = mesh;
        state.meshRevision = mesh ? mesh->GetRevision() : 0;
        state.localBounds = mesh ? mesh->GetLocalBounds() : bounds3();
    }

    if (updateTransform)
    {
        TransformComponent* transform = world->GetTransformComponent(renderer->GetEnsId());
        state.localToWorld = transform ? transform->worldMatrix : matrix4x4();
    }

    if (meshChanged || updateTransform)
    {
        state.worldBounds = state.localBounds.valid
            ? RenderMath::TransformBounds(state.localToWorld, state.localBounds)
            : bounds3();
        state.worldPosition = state.worldBounds.center;
    }
}
