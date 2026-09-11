#include "Scripting/NativeGameModule.h"

extern "C" void OrbedenGameNative_RegisterReflection();

namespace
{
    //游戏原生模块向 Editor 暴露的入口。
    //这里没有任何项目相关内容，所以由 SDK 提供、各游戏工程直接引用，不再是项目文件。
    //registerReflection 由 MetaGen 生成到游戏工程的生成目录里。
    const OrbedenNativeGameModuleApi ModuleApi
    {
        OrbedenNativeGameModuleAbiVersion,
        sizeof(OrbedenNativeGameModuleApi),
        nullptr,    //moduleName 保留在 ABI 中，当前没有消费方
        &OrbedenGameNative_RegisterReflection,
    };
}

extern "C" ORBEDEN_GAME_MODULE_EXPORT const OrbedenNativeGameModuleApi* OrbedenGameNative_GetApi()
{
    return &ModuleApi;
}
