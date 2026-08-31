#include "Scripting/ScriptSystem.h"

#include "Log/Log.h"
#include "Runtime/Native/OrbedenNativeApi.h"

#if defined(ORBEDEN_PLAYER)
extern "C"
{
    void ORBEDEN_NATIVE_CALL OrbedenGame_Initialize(void* nativeApi);
    void ORBEDEN_NATIVE_CALL OrbedenGame_Shutdown();
    void ORBEDEN_NATIVE_CALL OrbedenGame_Update(float deltaTime);
    void ORBEDEN_NATIVE_CALL OrbedenGame_FixedUpdate(float fixedDeltaTime);
    void ORBEDEN_NATIVE_CALL OrbedenGame_DrawGui();
}
#endif

//判断脚本入口是否完整
bool ScriptEntryPoints::IsValid() const
{
    return initialize && shutdown && update && fixedUpdate && drawGui;
}




#if defined(ORBEDEN_PLAYER)
//**Player**：绑定 NativeAOT 游戏模块的链接期导出入口
void ScriptSystem::SetAotEntryPoints()
{
    entryPoints.initialize = &OrbedenGame_Initialize;
    entryPoints.shutdown = &OrbedenGame_Shutdown;
    entryPoints.update = &OrbedenGame_Update;
    entryPoints.fixedUpdate = &OrbedenGame_FixedUpdate;
    entryPoints.drawGui = &OrbedenGame_DrawGui;
}
#endif

//**Editor**：设置 CLR 游戏程序集导出的完整入口
bool ScriptSystem::SetClrEntryPoints(const ScriptEntryPoints& value)
{
    if (runtimeMode != ScriptRuntimeMode::CLR || initialized || !value.IsValid()) return false;

    entryPoints = value;
    return true;
}



//准备脚本系统依赖和入口
bool ScriptSystem::OnInitialize(Application& app)
{
    renderSystem = app.GetSystem<RenderSystem>();
    if (!renderSystem) return false;

    runtimeMode = app.GetScriptRuntimeMode();
    if (runtimeMode == ScriptRuntimeMode::CLR) return true;

#if defined(ORBEDEN_PLAYER)
    SetAotEntryPoints();
    if (!Initialize()) return false;

    return true;
#else
    Log::Error("ScriptSystem AOT mode is unavailable in this Core build.");
    return false;
#endif
}

//关闭脚本模块
void ScriptSystem::OnShutdown()
{
    Shutdown();
    renderSystem = nullptr;
}

//初始化当前模式的游戏脚本模块
bool ScriptSystem::Initialize()
{
    if (initialized) return true;
    if (!entryPoints.IsValid())
    {
        Log::Error("ScriptSystem initialize failed: script entry points are incomplete.");
        return false;
    }

    OrbedenNativeApi nativeApi = OrbedenNativeApi::Create();
    entryPoints.initialize(&nativeApi);
    renderSystem->SetRenderOverlay(this);
    renderOverlayAttached = true;
    initialized = true;
    Log::Info(runtimeMode == ScriptRuntimeMode::AOT
        ? "AOT ScriptSystem initialized."
        : "CLR ScriptSystem initialized.");
    return true;
}

//关闭当前游戏脚本模块
void ScriptSystem::Shutdown()
{
    if (initialized && entryPoints.shutdown)
    {
        entryPoints.shutdown();
    }

    if (renderOverlayAttached && renderSystem)
    {
        renderSystem->SetRenderOverlay(nullptr);
    }

    entryPoints = ScriptEntryPoints();
    initialized = false;
    renderOverlayAttached = false;
}

//每帧更新游戏脚本。
//Editor CLR 模式调用 EditorPlayMode 绑定并注入的 C# 函数指针；
//Player AOT 模式调用构建期链接的 OrbedenGame_Update 导出符号。
void ScriptSystem::Update(World& world, float deltaTime)
{
    (void)world;
    if (initialized && entryPoints.update)
    {
        entryPoints.update(deltaTime);
    }
}

//固定步长更新游戏脚本。
void ScriptSystem::FixedUpdate(World& world, float fixedDeltaTime)
{
    (void)world;
    if (initialized && entryPoints.fixedUpdate)
    {
        entryPoints.fixedUpdate(fixedDeltaTime);
    }
}

//绘制游戏脚本 GUI。
//Editor CLR 模式调用 EditorPlayMode 绑定并注入的 C# 函数指针；
//Player AOT 模式调用构建期链接的 OrbedenGame_DrawGui 导出符号。
void ScriptSystem::DrawOverlay()
{
    if (initialized && entryPoints.drawGui)
    {
        entryPoints.drawGui();
    }
}

//判断游戏脚本模块是否可用
bool ScriptSystem::IsInitialized() const
{
    return initialized;
}
