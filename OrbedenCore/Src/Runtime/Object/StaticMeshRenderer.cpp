#include "Runtime/Object/StaticMeshRenderer.h"

#include "Rendering/RenderScene.h"
#include "Runtime/Ens.h"

OBJECT_TYPE_IMPLEMENT(StaticMeshRenderer, Component)

//获取启用状态
bool StaticMeshRenderer::GetEnabled() const
{
    return enabled;
}

//设置启用状态并同步渲染场景注册
void StaticMeshRenderer::SetEnabled(bool value)
{
    if (enabled == value) return;

    enabled = value;
    SyncRenderSceneRegistration();
}

//判断当前组件是否应注册到渲染场景
bool StaticMeshRenderer::IsRenderSceneEligible() const
{
    Ens* ens = GetEns();
    return enabled && ens && ens->GetWorldActive();
}

//按当前状态同步渲染场景注册
void StaticMeshRenderer::SyncRenderSceneRegistration()
{
    RenderScene* scene = GetRenderScene();
    if (!scene) return;

    if (IsRenderSceneEligible())
    {
        scene->RegisterRenderer(this);
    }
    else
    {
        scene->UnregisterRenderer(this);
    }
}

//挂载时注册到当前渲染场景
void StaticMeshRenderer::OnAttach()
{
    SyncRenderSceneRegistration();
}

//卸载时注销并释放脚本设置的网格资源引用
void StaticMeshRenderer::OnDetach()
{
    RenderScene* scene = GetRenderScene();
    if (scene) scene->UnregisterRenderer(this);
    mesh.SetInstanceId(StringId());
}

//所属 Ens 的 worldActive 变化时同步渲染场景注册
void StaticMeshRenderer::OnWorldActiveChanged(bool worldActive)
{
    (void)worldActive;
    SyncRenderSceneRegistration();
}
