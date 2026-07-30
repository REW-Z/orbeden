#include "Runtime/Object/Camera.h"

#include "Rendering/RenderScene.h"

OBJECT_TYPE_IMPLEMENT(Camera, Component)

//获取启用状态
bool Camera::GetEnabled() const
{
    return enabled;
}

//设置启用状态并同步渲染场景注册
void Camera::SetEnabled(bool value)
{
    if (enabled == value) return;

    enabled = value;
    RenderScene* scene = GetRenderScene();
    if (!scene) return;

    if (enabled)
    {
        renderSceneHandle = scene->RegisterCamera(this);
    }
    else
    {
        scene->UnregisterCamera(renderSceneHandle);
        renderSceneHandle = RenderSceneHandle();
    }
}

//挂载时注册到当前渲染场景
void Camera::OnAttach()
{
    RenderScene* scene = GetRenderScene();
    if (enabled && scene) renderSceneHandle = scene->RegisterCamera(this);
}

//卸载时从当前渲染场景注销
void Camera::OnDetach()
{
    RenderScene* scene = GetRenderScene();
    if (scene) scene->UnregisterCamera(renderSceneHandle);
    renderSceneHandle = RenderSceneHandle();
}
