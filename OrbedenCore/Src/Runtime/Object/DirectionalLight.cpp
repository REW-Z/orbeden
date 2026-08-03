#include "Runtime/Object/DirectionalLight.h"

#include "Rendering/RenderScene.h"
#include "Runtime/Ens.h"

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
    SyncRenderSceneRegistration();
}

//判断当前组件是否应注册到渲染场景
bool DirectionalLight::IsRenderSceneEligible() const
{
    Ens* ens = GetEns();
    return enabled && ens && ens->GetWorldActive();
}

//按当前状态同步渲染场景注册
void DirectionalLight::SyncRenderSceneRegistration()
{
    RenderScene* scene = GetRenderScene();
    if (!scene) return;

    if (IsRenderSceneEligible())
    {
        scene->RegisterDirectionalLight(this);
    }
    else
    {
        scene->UnregisterDirectionalLight(this);
    }
}

//挂载时注册到当前渲染场景
void DirectionalLight::OnAttach()
{
    SyncRenderSceneRegistration();
}

//卸载时从当前渲染场景注销
void DirectionalLight::OnDetach()
{
    RenderScene* scene = GetRenderScene();
    if (scene) scene->UnregisterDirectionalLight(this);
}

//所属 Ens 的 worldActive 变化时同步渲染场景注册
void DirectionalLight::OnWorldActiveChanged(bool worldActive)
{
    (void)worldActive;
    SyncRenderSceneRegistration();
}
