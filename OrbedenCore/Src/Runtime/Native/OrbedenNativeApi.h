#pragma once

#include "Runtime/Gui/RuntimeGuiBridge.h"
#include "Runtime/Native/OrbedenEngineNativeApi.h"
#include "Scripting/ScriptInterop.h"

class World;

#pragma pack(push, 8)

//C# ScriptBehaviour Wrapper 使用的原生宿主函数表。
struct ScriptBehaviourBindApi
{
    void* Context = nullptr;
    void* GetHostCount = nullptr;
    void* GetHostAt = nullptr;
    void* CreateHost = nullptr;
    void* RemoveHost = nullptr;
    void* GetEns = nullptr;
    void* GetTypeName = nullptr;
    void* GetEnabled = nullptr;
    void* SetEnabled = nullptr;
    void* GetFieldCount = nullptr;
    void* GetFieldName = nullptr;
    void* GetFieldTypeName = nullptr;
    void* GetFieldKind = nullptr;
    void* GetFieldValue = nullptr;
    void* SetField = nullptr;
    void* ResolveReference = nullptr;

    //创建绑定到指定 World 的宿主函数表。
    static ScriptBehaviourBindApi Create(World* world);
};

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
    ScriptBehaviourBindApi ScriptBehaviour;

    //创建完整原生 API 函数表。
    static OrbedenNativeApi Create(::World* world);
};

#pragma pack(pop)

ORBEDEN_ASSERT_NATIVE_API_TABLE(ScriptBehaviourBindApi, 16);
ORBEDEN_ASSERT_NATIVE_API_TABLE(OrbedenNativeApi, 256);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenNativeApi, Gui, 0);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenNativeApi, Collider, 152);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenNativeApi, ObjectExtension, 230);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenNativeApi, ScriptInterop, 231);
ORBEDEN_ASSERT_NATIVE_API_SLOT(OrbedenNativeApi, ScriptBehaviour, 240);
