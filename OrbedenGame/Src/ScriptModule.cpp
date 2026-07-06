#include "ScriptModule.h"

#include "Log/Log.h"
#include "Runtime/Native/NativeCall.h"
#include "Runtime/Native/OrbedenNativeApi.h"

extern "C"
{
    void ORBEDEN_NATIVE_CALL OrbedenGame_Initialize(void* nativeApi);
    void ORBEDEN_NATIVE_CALL OrbedenGame_Shutdown();
    void ORBEDEN_NATIVE_CALL OrbedenGame_Update(float deltaTime);
    void ORBEDEN_NATIVE_CALL OrbedenGame_DrawGui();
}

bool ScriptModule::Initialize()
{
    if (initialized) return true;

    //初始化静态链接的 AOT 游戏模块。
    OrbedenNativeApi nativeApi = OrbedenNativeApi::Create();
    OrbedenGame_Initialize(&nativeApi);
    initialized = true;
    Log::Info("AOT ScriptModule initialized.");
    return true;
}

void ScriptModule::Shutdown()
{
    if (!initialized) return;

    OrbedenGame_Shutdown();
    initialized = false;
}

void ScriptModule::Update(World& world, float deltaTime)
{
    (void)world;
    if (!initialized) return;

    OrbedenGame_Update(deltaTime);
}

void ScriptModule::DrawOverlay()
{
    if (!initialized) return;

    OrbedenGame_DrawGui();
}

bool ScriptModule::IsInitialized() const
{
    return initialized;
}
