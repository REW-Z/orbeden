#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/RenderTypes.h"
#include "Runtime/Object/DirectionalLight.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"
#include "Runtime/RenderSettings.h"

class Camera;
class Component;
class TransformCache;
class StaticMeshRenderer;
class World;

//相机渲染快照，保存一次场景更新所需的视图和投影数据
struct RenderCamera
{
public:
    EnsId ens;
    Camera* camera = nullptr;

    matrix4x4 worldMatrix;
    matrix4x4 viewMatrix;
    matrix4x4 projectionMatrix;
    matrix4x4 viewProjectionMatrix;
    frustum viewFrustum;

    vector3 position;
    float32 depth = 0.0f;
    float32 fieldOfView = 60.0f;
    float32 nearPlane = 0.1f;
    float32 farPlane = 1000.0f;

    uint32 drawLayerMask = 0xFFFFFFFFu;
    ClearMode clearMode = ClearMode::SolidColor;
    color clearColor = { 0.1f, 0.12f, 0.16f, 1.0f };

    RenderTargetID renderTargetId;
    GpuRenderTargetID renderTarget;
    GpuRenderTargetID cameraTextureTarget;
    GpuTextureID cameraColorTexture;
    GpuDepthTextureID cameraDepthTexture;

    float32 normalizedViewportX = 0.0f;
    float32 normalizedViewportY = 0.0f;
    float32 normalizedViewportWidth = 1.0f;
    float32 normalizedViewportHeight = 1.0f;
    int32 viewportX = 0;
    int32 viewportY = 0;
    int32 viewportWidth = 0;
    int32 viewportHeight = 0;
    float32 elapsedTime = 0.0f;
};

//相机可见渲染器记录
struct VisibleItem
{
public:
    StaticMeshRenderer* renderer = nullptr;
    float32 cameraDistance = 0.0f;
};

//相机剔除后临时展开的子网格绘制项
struct RenderItem
{
public:
    EnsId ens;
    StaticMeshRenderer* renderer = nullptr;
    Mesh* mesh = nullptr;
    Material* material = nullptr;

    uint32 subMeshIndex = 0;
    uint32 indexStart = 0;
    uint32 indexCount = 0;

    uint32 drawLayer = 1u;
    DrawQueue drawQueue = DrawQueue::Opaque;
    float32 cameraDistance = 0.0f;

    matrix4x4 localToWorld;
    bounds3 localBounds;
    bounds3 worldBounds;
    vector3 worldPosition;

    bool castShadows = true;
    bool receiveShadows = true;
};

//方向光渲染快照，保存光照和阴影参数
struct RenderDirectionalLight
{
public:
    EnsId ens;
    DirectionalLight* light = nullptr;
    vector3 direction = { -0.35f, -1.0f, -0.45f };
    color color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float32 intensity = 1.0f;
    bool castShadows = true;
    float32 shadowBias = 0.004f;
    float32 shadowStrength = 0.45f;
    float32 shadowDistance = 24.0f;
};

//单个相机的可见渲染器和临时绘制项
struct VisibleSet
{
public:
    RenderCamera camera;
    List<VisibleItem> visibleItems;
    List<RenderItem> renderItems;

    //清空上一相机留下的临时数据
    void Clear();
};

//持久渲染场景，组件主动注册并由渲染帧增量刷新
class RenderScene
{
private:
    enum class ComponentType
    {
        Camera,
        DirectionalLight,
        Renderer,
    };

    struct PendingChange
    {
        ComponentType type = ComponentType::Renderer;
        bool add = false;
        Component* component = nullptr;
    };

    World* world = nullptr;
    List<Camera*> cameraComponents;
    List<DirectionalLight*> directionalLightComponents;
    List<PendingChange> pendingChanges;
    uint32 readDepth = 0;

    //应用等待安全阶段处理的增删操作
    void FlushPendingChanges();

    //取消尚未进入指针列表的注册
    bool CancelPendingAdd(ComponentType type, Component* component);

    //激活相机注册
    void AddCamera(Camera* camera);

    //移除相机注册
    void RemoveCamera(Camera* camera);

    //激活方向光注册
    void AddDirectionalLight(DirectionalLight* light);

    //移除方向光注册
    void RemoveDirectionalLight(DirectionalLight* light);

    //激活渲染器注册
    void AddRenderer(StaticMeshRenderer* renderer);

    //移除渲染器注册
    void RemoveRenderer(StaticMeshRenderer* renderer);

    //刷新指定渲染器的变换和包围盒
    void UpdateRenderer(StaticMeshRenderer* renderer, bool updateTransform);

public:
    RenderSettings renderSettings;
    List<RenderCamera> cameras;
    List<RenderDirectionalLight> directionalLights;
    List<StaticMeshRenderer*> renderers;

    //绑定世界并完整收集一次已有渲染组件
    void BindWorld(World& currentWorld);

    //解除世界绑定并清空全部持久记录
    void UnbindWorld();

    //增量刷新变换状态和组件快照
    void Update(World& currentWorld, TransformCache& transformCache);

    //读取阶段 不允许修改组件指针列表
    void BeginRead();

    //结束读取阶段并应用延迟增删
    void EndRead();

    //注册启用的相机组件
    void RegisterCamera(Camera* camera);

    //注销相机组件
    void UnregisterCamera(Camera* camera);

    //注册启用的方向光组件
    void RegisterDirectionalLight(DirectionalLight* light);

    //注销方向光组件
    void UnregisterDirectionalLight(DirectionalLight* light);

    //注册启用的静态网格渲染器
    void RegisterRenderer(StaticMeshRenderer* renderer);

    //注销静态网格渲染器
    void UnregisterRenderer(StaticMeshRenderer* renderer);

    //把相机可见渲染器展开为临时子网格绘制项
    void BuildRenderItems(VisibleSet& visibleSet) const;
};

//获取当前活动的持久渲染场景
RenderScene* GetRenderScene();
