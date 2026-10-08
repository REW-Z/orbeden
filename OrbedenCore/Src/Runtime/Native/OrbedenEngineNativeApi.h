#pragma once

#include "Runtime/Native/RuntimeComponentBinds.h"
#include "Runtime/Native/RuntimeResourceBinds.h"
#include "Runtime/Native/NativeBindings.h"
#include "Runtime/Gui/RetainedGuiBridge.h"
#include "Runtime/Gui/RuntimeGuiBridge.h"

#pragma pack(push, 8)

//传给 Editor 的引擎原生 API。
struct OrbedenEngineNativeApi
{
public:
    uint32 abiVersion = 3;
    uint32 structSize = sizeof(OrbedenEngineNativeApi);
    WorldBind World;
    PathDefinesBind PathDefines;
    EnsBind Ens;
    ObjectBind Object;
    ObjectExtensionBind ObjectExtension;
    NativeBindingsApi Bindings;
    RuntimeGuiApi Gui;
    RuntimeGuiExtensionApi GuiExtension;
    RuntimeGuiAdvancedApi GuiAdvanced;
    RuntimeGuiCurveApi GuiCurve;
    //尾部追加：RetainedGUI 函数表地址，唯一不动既有槽位的扩表方式；托管侧直接按表解引用。
    void* GetRetainedGuiApi = nullptr;

    //创建引擎原生 API 函数表。
    static OrbedenEngineNativeApi Create();
};

#pragma pack(pop)

static_assert(sizeof(OrbedenEngineNativeApi) == 8 + sizeof(void*) * 72);
static_assert(offsetof(OrbedenEngineNativeApi, World) == 8);
static_assert(offsetof(OrbedenEngineNativeApi, Bindings) == 8 + sizeof(void*) * 27);
