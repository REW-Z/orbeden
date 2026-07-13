#pragma once

#include "Rendering/Backend/RenderBackend.h"
#include "Rendering/RenderTypes.h"
#include "Runtime/Object/DirectionalLight.h"
#include "Runtime/RenderSettings.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Mesh.h"

//每帧相机记录，保存从场景相机计算出的渲染所需数据。
struct RenderCamera
{
public:
    EnsId ens;
    Camera* camera = nullptr;

    //相机世界变换以及用于视锥剔除和投影的矩阵。
    matrix4x4 worldMatrix;
    matrix4x4 viewMatrix;
    matrix4x4 projectionMatrix;
    matrix4x4 viewProjectionMatrix;
    frustum viewFrustum;

    //相机位置和透视参数。
    vector3 position;
    float32 depth = 0.0f;
    float32 fieldOfView = 60.0f;
    float32 nearPlane = 0.1f;
    float32 farPlane = 1000.0f;

    //决定相机可以绘制哪些层，以及 pass 的清屏方式。
    uint32 drawLayerMask = 0xFFFFFFFFu;
    ClearMode clearMode = ClearMode::SolidColor;
    color clearColor = { 0.1f, 0.12f, 0.16f, 1.0f };

    //目标为 0 时绘制到主 framebuffer，否则绘制到指定离屏目标。
    RenderTargetID renderTargetId;
    GpuRenderTargetID renderTarget;

    //归一化 viewport 来自场景配置，像素 viewport 由渲染系统按目标尺寸解析。
    float32 normalizedViewportX = 0.0f;
    float32 normalizedViewportY = 0.0f;
    float32 normalizedViewportWidth = 1.0f;
    float32 normalizedViewportHeight = 1.0f;
    int32 viewportX = 0;
    int32 viewportY = 0;
    int32 viewportWidth = 0;
    int32 viewportHeight = 0;
};

//每帧绘制项记录，描述一个静态网格子网格的世界状态和绘制属性。
struct RenderItem
{
public:
    EnsId ens;
    StaticMeshRenderer* renderer = nullptr;
    Mesh* mesh = nullptr;
    Material* material = nullptr;

    //子网格在共享索引缓冲中的绘制范围。
    uint32 subMeshIndex = 0;
    uint32 indexStart = 0;
    uint32 indexCount = 0;

    //用于相机层过滤和透明/不透明队列排序。
    uint32 drawLayer = 1u;
    DrawQueue drawQueue = DrawQueue::Opaque;

    //对象的局部到世界变换，以及剔除使用的包围盒。
    matrix4x4 localToWorld;
    bounds3 localBounds;
    bounds3 worldBounds;
    vector3 worldPosition;

    //阴影 pass 和主 pass 是否应处理该对象。
    bool castShadows = true;
    bool receiveShadows = true;
};

//每帧方向光记录，保存光照和阴影 pass 所需的参数。
struct RenderDirectionalLight
{
public:
    EnsId ens;
    DirectionalLight* light = nullptr;

    //方向使用世界空间向量，颜色和强度用于主光照计算。
    vector3 direction = { -0.35f, -1.0f, -0.45f };
    color color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float32 intensity = 1.0f;

    //阴影开关及其采样参数。
    bool castShadows = true;
    float32 shadowBias = 0.004f;
    float32 shadowStrength = 0.45f;
    float32 shadowDistance = 24.0f;
};

//每帧渲染场景，由场景构建阶段填充并在当前帧内只读消费。
class RenderScene
{
public:
    //当前帧的全局渲染设置和相机、灯光、绘制项列表。
    RenderSettings renderSettings;
    List<RenderCamera> cameras;
    List<RenderDirectionalLight> directionalLights;
    List<RenderItem> items;

    //清空当前帧数据，供下一次场景构建复用容器。
    void Clear();
};

//单个相机的紧凑可见项，仅保存 RenderScene::items 中的索引和排序距离。
struct VisibleItem
{
public:
    uint32 itemIndex = 0;
    float32 cameraDistance = 0.0f;
};

//单个相机的可见集合，由剔除阶段生成并由排序和渲染阶段消费。
struct VisibleSet
{
public:
    RenderCamera camera;
    List<VisibleItem> items;

    //清空上一相机留下的可见项。
    void Clear();
};
