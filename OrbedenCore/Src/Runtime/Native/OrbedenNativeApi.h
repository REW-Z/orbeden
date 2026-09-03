#pragma once

#include "Runtime/Gui/RuntimeGuiBridge.h"
#include "Runtime/Native/OrbedenEngineNativeApi.h"
#include "Scripting/ScriptInterop.h"

#pragma pack(push, 8)

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
    ScriptInterop::ScriptInteropApi ScriptInterop;

    //创建完整原生 API 函数表。
    static OrbedenNativeApi Create();
};

#pragma pack(pop)

ORBEDEN_ASSERT_NATIVE_API_TABLE(OrbedenNativeApi, 240);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenNativeApi, Gui, 0);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenNativeApi, Collider, 152);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenNativeApi, ObjectExtension, 230);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenNativeApi, ScriptInterop, 231);
