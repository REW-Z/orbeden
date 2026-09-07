#include "Scripting/NativeGameModule.h"

extern "C" void OrbedenGameNative_RegisterReflection();

namespace
{
    const OrbedenNativeGameModuleApi ModuleApi
    {
        OrbedenNativeGameModuleAbiVersion,
        sizeof(OrbedenNativeGameModuleApi),
        "{{PROJECT_NAME}}Native",
        &OrbedenGameNative_RegisterReflection,
    };
}

extern "C" ORBEDEN_GAME_MODULE_EXPORT const OrbedenNativeGameModuleApi* OrbedenGameNative_GetApi()
{
    return &ModuleApi;
}
