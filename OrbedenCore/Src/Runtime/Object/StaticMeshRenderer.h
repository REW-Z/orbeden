#pragma once

#include "Rendering/RenderTypes.h"
#include "Runtime/EnsId.h"
#include "Runtime/Object/Mesh.h"

class RenderScene;

//静态网格渲染组件
class StaticMeshRenderer : public Component
{
    OBJECT_TYPE_DECLARE(StaticMeshRenderer)

private:
    friend class RenderScene;

    bool enabled = true;
    RenderSceneHandle renderSceneHandle;

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

    //挂载时注册到当前渲染场景
    void OnAttach() override;

    //卸载时注销并释放脚本设置的网格资源引用
    void OnDetach() override;
};
