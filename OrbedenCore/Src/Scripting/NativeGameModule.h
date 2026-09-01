#pragma once

#include "Defines/types.h"

constexpr uint32 OrbedenNativeGameModuleAbiVersion = 1;

//游戏原生 DLL 向 Editor 暴露的版本化模块描述。
struct OrbedenNativeGameModuleApi
{
    uint32 abiVersion = OrbedenNativeGameModuleAbiVersion;
    uint32 structSize = sizeof(OrbedenNativeGameModuleApi);
    const char* moduleName = nullptr;
    void (*registerReflection)() = nullptr;
};

using GetOrbedenNativeGameModuleApi = const OrbedenNativeGameModuleApi* (*)();

#if defined(_WIN32)
#define ORBEDEN_GAME_MODULE_EXPORT __declspec(dllexport)
#else
#define ORBEDEN_GAME_MODULE_EXPORT __attribute__((visibility("default")))
#endif

constexpr const char* OrbedenNativeGameModuleEntryPoint = "OrbedenGameNative_GetApi";
