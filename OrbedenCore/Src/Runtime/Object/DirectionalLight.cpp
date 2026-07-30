#include "Runtime/Object/DirectionalLight.h"

#include "Rendering/RenderScene.h"

OBJECT_TYPE_IMPLEMENT(DirectionalLight, Component)

//获取启用状态
bool DirectionalLight::GetEnabled() const
{
    return enabled;
}

//设置启用状态并同步渲染场景注册
void DirectionalLight::SetEnabled(bool value)
{
    if (enabled == value) return;

    enabled = value;
    RenderScene* scene = GetRenderScene();
    if (!scene) return;

    if (enabled)
    {
        renderSceneHandle = scene->RegisterDirectionalLight(this);
    }
    else
    {
        scene->UnregisterDirectionalLight(renderSceneHandle);
        renderSceneHandle = RenderSceneHandle();
    }
}

//挂载时注册到当前渲染场景
void DirectionalLight::OnAttach()
{
    RenderScene* scene = GetRenderScene();
    if (enabled && scene) renderSceneHandle = scene->RegisterDirectionalLight(this);
}

//卸载时从当前渲染场景注销
void DirectionalLight::OnDetach()
{
    RenderScene* scene = GetRenderScene();
    if (scene) scene->UnregisterDirectionalLight(renderSceneHandle);
    renderSceneHandle = RenderSceneHandle();
}
