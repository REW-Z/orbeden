#pragma once

#include "Runtime/Gui/RuntimeGuiBridge.h"
#include "Runtime/Native/RuntimeComponentBinds.h"
#include "Runtime/Native/RuntimeResourceBinds.h"

//传给 AOT GameModule 的引擎原生 API。
struct OrbedenNativeApi
{
public:
    RuntimeGuiApi Gui;
    WorldBind World;
    PathDefinesBind PathDefines;
    EnsBind Ens;
    TransformComponentBind TransformComponent;
    StaticMeshRendererBind StaticMeshRenderer;
    ObjectBind Object;
    MeshBind Mesh;
    MaterialBind Material;
    ShaderBind Shader;
    RigidBodyBind RigidBody;
    ColliderBind Collider;
    CharacterControllerBind CharacterController;
    RuntimeGuiExtensionApi GuiExtension;
    RuntimeGuiAdvancedApi GuiAdvanced;
    ObjectExtensionBind ObjectExtension;

    //创建完整原生 API 函数表。
    static OrbedenNativeApi Create();
};
