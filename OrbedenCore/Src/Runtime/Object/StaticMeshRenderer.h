#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Material.h"
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
    ORBEDEN_COMPONENT_UNIQUE

private:
    friend class RenderScene;
    friend class SceneCuller;
    friend class ForwardPipeline;

    bool enabled = true;
    Ref<Mesh> runtimeMesh;
    StaticMeshRendererRenderState renderState;

    //按当前状态同步渲染场景注册
    void SyncRenderSceneRegistration();

public:
    //持久化源网格；程序生成的网格通过 SetRuntimeMesh 覆盖渲染。
    Ref<Mesh> mesh;
    //按子网格槽位给出的材质；槽位为空或超出数组长度的子网格不绘制。
    List<Ref<Material>> materials;
    uint32 drawLayer = 1u;
    DrawQueue drawQueue = DrawQueue::Opaque;
    bool castShadows = true;
    bool receiveShadows = true;

    /// <summary>设置非持久化渲染网格，传空恢复源网格；对象由调用方管理。</summary>
    ORBEDEN_BIND_IGNORE
    void SetRuntimeMesh(Mesh* value);

    /// <summary>获取实际渲染网格，运行时覆盖失效时恢复源网格。</summary>
    ORBEDEN_BIND_IGNORE
    Mesh* GetRenderMesh() const;

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
