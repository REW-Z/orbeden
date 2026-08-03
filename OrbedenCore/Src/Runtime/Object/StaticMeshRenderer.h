#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EnsId.h"
#include "Runtime/Object/Mesh.h"

class RenderScene;
class SceneCuller;
class ForwardPipeline;

//渲染器运行时缓存，避免每个相机重复计算变换和包围盒
struct StaticMeshRendererRenderState
{
    Mesh* mesh = nullptr;
    matrix4x4 localToWorld;
    bounds3 localBounds;
    bounds3 worldBounds;
    vector3 worldPosition;
};

//静态网格渲染组件
class StaticMeshRenderer : public Component
{
    OBJECT_TYPE_DECLARE(StaticMeshRenderer)

private:
    friend class RenderScene;
    friend class SceneCuller;
    friend class ForwardPipeline;

    bool enabled = true;
    StaticMeshRendererRenderState renderState;

    //按当前状态同步渲染场景注册
    void SyncRenderSceneRegistration();

public:
    Ref<Mesh> mesh;
    uint32 drawLayer = 1u;
    DrawQueue drawQueue = DrawQueue::Opaque;
    bool castShadows = true;
    bool receiveShadows = true;

    //获取启用状态
    bool GetEnabled() const;

    //设置启用状态并同步渲染场景注册
    void SetEnabled(bool value);

    //判断当前组件是否应注册到渲染场景
    bool IsRenderSceneEligible() const;

    //挂载时注册到当前渲染场景
    void OnAttach() override;

    //卸载时注销并释放脚本设置的网格资源引用
    void OnDetach() override;

    //所属 Ens 的 worldActive 变化时同步渲染场景注册
    void OnWorldActiveChanged(bool worldActive) override;
};
