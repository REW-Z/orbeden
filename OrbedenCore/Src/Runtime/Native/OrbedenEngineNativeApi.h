#pragma once

#include "Runtime/Native/RuntimeComponentBinds.h"
#include "Runtime/Native/RuntimeResourceBinds.h"

//传给 Editor 的引擎原生 API。
struct OrbedenEngineNativeApi
{
public:
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
    ObjectExtensionBind ObjectExtension;

    //创建引擎原生 API 函数表。
    static OrbedenEngineNativeApi Create();
};
