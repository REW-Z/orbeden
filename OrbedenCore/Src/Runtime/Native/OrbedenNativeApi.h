#pragma once

#include "Runtime/Gui/RuntimeGuiBridge.h"
#include "Runtime/Native/OrbedenEngineNativeApi.h"
#include "Scripting/ScriptInterop.h"

class World;

#pragma pack(push, 8)

//C# Script Wrapper 使用的原生宿主函数表。
struct ScriptBindApi
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
    static ScriptBindApi Create(World* world);
};

//传给 AOT GameModule 的引擎原生 API。
struct OrbedenNativeApi
{
public:
    uint32 abiVersion = 2;
    uint32 structSize = sizeof(OrbedenNativeApi);
    RuntimeGuiApi Gui;
    WorldBind World;
    PathDefinesBind PathDefines;
    EnsBind Ens;
    ObjectBind Object;
    RuntimeGuiExtensionApi GuiExtension;
    RuntimeGuiAdvancedApi GuiAdvanced;
    ObjectExtensionBind ObjectExtension;
    NativeBindingsApi Bindings;
    ScriptInterop::ScriptInteropApi ScriptInterop;
    ScriptBindApi Script;
    RuntimeGuiDrawApi GuiDraw;

    //创建完整原生 API 函数表。
    static OrbedenNativeApi Create(::World* world);
};

#pragma pack(pop)

ORBEDEN_ASSERT_NATIVE_API_TABLE(ScriptBindApi, 16);
static_assert(sizeof(OrbedenNativeApi) == 8 + sizeof(void*) * 101);
static_assert(offsetof(OrbedenNativeApi, Gui) == 8);
static_assert(offsetof(OrbedenNativeApi, Bindings) == 8 + sizeof(void*) * 51);
