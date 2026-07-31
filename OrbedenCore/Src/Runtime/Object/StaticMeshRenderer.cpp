#include "Runtime/Object/StaticMeshRenderer.h"

#include "Rendering/RenderScene.h"

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
    RenderScene* scene = GetRenderScene();
    if (!scene) return;

    if (enabled)
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
    RenderScene* scene = GetRenderScene();
    if (enabled && scene) scene->RegisterRenderer(this);
}

//卸载时注销并释放脚本设置的网格资源引用
void StaticMeshRenderer::OnDetach()
{
    RenderScene* scene = GetRenderScene();
    if (scene) scene->UnregisterRenderer(this);
    mesh.SetInstanceId(StringId());
}
