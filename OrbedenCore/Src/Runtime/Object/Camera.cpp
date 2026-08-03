#include "Runtime/Object/Camera.h"

#include "Rendering/RenderScene.h"
#include "Runtime/Ens.h"

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
    SyncRenderSceneRegistration();
}

//判断当前组件是否应注册到渲染场景
bool Camera::IsRenderSceneEligible() const
{
    Ens* ens = GetEns();
    return enabled && ens && ens->GetWorldActive();
}

//按当前状态同步渲染场景注册
void Camera::SyncRenderSceneRegistration()
{
    RenderScene* scene = GetRenderScene();
    if (!scene) return;

    if (IsRenderSceneEligible())
    {
        scene->RegisterCamera(this);
    }
    else
    {
        scene->UnregisterCamera(this);
    }
}

//挂载时注册到当前渲染场景
void Camera::OnAttach()
{
    SyncRenderSceneRegistration();
}

//卸载时从当前渲染场景注销
void Camera::OnDetach()
{
    RenderScene* scene = GetRenderScene();
    if (scene) scene->UnregisterCamera(this);
}

//所属 Ens 的 worldActive 变化时同步渲染场景注册
void Camera::OnWorldActiveChanged(bool worldActive)
{
    (void)worldActive;
    SyncRenderSceneRegistration();
}
