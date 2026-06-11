#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/DirectionalLight.h"
#include "Runtime/RenderSettings.h"
#include "Runtime/Resources/Material.h"
#include "Runtime/Resources/Mesh.h"

//每帧相机记录
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
    int32 viewportWidth = 0;
    int32 viewportHeight = 0;
};

//每帧绘制项记录
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
    matrix4x4 localToWorld;
    bounds3 localBounds;
    bounds3 worldBounds;
    vector3 worldPosition;
    float32 cameraDistance = 0.0f;
    bool castShadows = true;
    bool receiveShadows = true;
};

//每帧方向光记录
struct RenderDirectionalLight
{
public:
    EnsId ens;
    DirectionalLight* light = nullptr;
    vector3 direction = { -0.35f, -1.0f, -0.45f };
    vector3 color = { 1.0f, 1.0f, 1.0f };
    float32 intensity = 1.0f;
    bool castShadows = true;
    float32 shadowBias = 0.004f;
    float32 shadowStrength = 0.45f;
    float32 shadowDistance = 24.0f;
};

//每帧渲染场景
class RenderScene
{
public:
    RenderSettings renderSettings;
    List<RenderCamera> cameras;
    List<RenderDirectionalLight> directionalLights;
    List<RenderItem> items;

    //清空当前帧数据
    void Clear();
};

//单个相机的可见集合
struct VisibleSet
{
public:
    RenderCamera camera;
    List<RenderItem> items;

    //清空可见集合
    void Clear();
};
