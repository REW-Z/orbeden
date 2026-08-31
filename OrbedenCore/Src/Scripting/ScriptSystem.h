#pragma once

#include "Application.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/Native/NativeCall.h"

//游戏脚本模块的固定原生入口
struct ScriptEntryPoints
{
public:
    using InitializeFunction = void(ORBEDEN_NATIVE_CALL*)(void*);
    using ShutdownFunction = void(ORBEDEN_NATIVE_CALL*)();
    using UpdateFunction = void(ORBEDEN_NATIVE_CALL*)(float);
    using FixedUpdateFunction = void(ORBEDEN_NATIVE_CALL*)(float);
    using DrawGuiFunction = void(ORBEDEN_NATIVE_CALL*)();

    InitializeFunction initialize = nullptr;
    ShutdownFunction shutdown = nullptr;
    UpdateFunction update = nullptr;
    FixedUpdateFunction fixedUpdate = nullptr;
    DrawGuiFunction drawGui = nullptr;

    //判断脚本入口是否完整
    bool IsValid() const;
};

//统一执行 NativeAOT Player 和 CLR PIE 游戏脚本
class ScriptSystem final : public IEngineSystem, public IRenderOverlay
{
private:
    ScriptEntryPoints entryPoints;
    RenderSystem* renderSystem = nullptr;
    ScriptRuntimeMode runtimeMode = ScriptRuntimeMode::AOT;
    bool initialized = false;
    bool renderOverlayAttached = false;

#if defined(ORBEDEN_PLAYER)
    //绑定 NativeAOT 游戏模块的链接期导出入口
    void SetAotEntryPoints();
#endif

public:
    //获取渲染依赖，并按运行模式准备脚本入口
    bool OnInitialize(Application& app) override;

    //关闭脚本模块
    void OnShutdown() override;

    //设置 CLR 游戏程序集导出的完整入口
    bool SetClrEntryPoints(const ScriptEntryPoints& value);

    //初始化当前模式的游戏脚本模块
    bool Initialize();

    //关闭当前游戏脚本模块
    void Shutdown();

    //每帧更新游戏脚本
    void Update(World& world, float deltaTime) override;

    //固定步长更新游戏脚本
    void FixedUpdate(World& world, float fixedDeltaTime) override;

    //绘制游戏脚本 GUI
    void DrawOverlay() override;

    //判断游戏脚本模块是否可用
    bool IsInitialized() const;
};
