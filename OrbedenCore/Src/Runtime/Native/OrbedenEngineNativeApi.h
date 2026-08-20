#pragma once

#include "Runtime/Native/RuntimeComponentBinds.h"
#include "Runtime/Native/RuntimeResourceBinds.h"

#pragma pack(push, 8)

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

#pragma pack(pop)

ORBEDEN_ASSERT_NATIVE_API_TABLE(OrbedenEngineNativeApi, 199);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenEngineNativeApi, World, 0);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenEngineNativeApi, Collider, 141);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenEngineNativeApi, ObjectExtension, 198);
