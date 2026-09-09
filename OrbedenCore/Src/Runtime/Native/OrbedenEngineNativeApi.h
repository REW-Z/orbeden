#pragma once

#include "Runtime/Native/RuntimeComponentBinds.h"
#include "Runtime/Native/RuntimeResourceBinds.h"
#include "Runtime/Native/NativeBindings.h"

#pragma pack(push, 8)

//传给 Editor 的引擎原生 API。
struct OrbedenEngineNativeApi
{
public:
    uint32 abiVersion = 2;
    uint32 structSize = sizeof(OrbedenEngineNativeApi);
    WorldBind World;
    PathDefinesBind PathDefines;
    EnsBind Ens;
    ObjectBind Object;
    ObjectExtensionBind ObjectExtension;
    NativeBindingsApi Bindings;

    //创建引擎原生 API 函数表。
    static OrbedenEngineNativeApi Create();
};

#pragma pack(pop)

static_assert(sizeof(OrbedenEngineNativeApi) == 8 + sizeof(void*) * 29);
static_assert(offsetof(OrbedenEngineNativeApi, World) == 8);
static_assert(offsetof(OrbedenEngineNativeApi, Bindings) == 8 + sizeof(void*) * 19);
