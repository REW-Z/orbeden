#pragma once

#include "Application.h"
#include "Rendering/RenderSystem.h"

//静态链接的 AOT 游戏模块。
class ScriptModule : public IEngineSystem, public IRenderOverlay
{
private:
    bool initialized = false;

public:
    //初始化 AOT 游戏模块。
    bool Initialize();

    //关闭 AOT 游戏模块。
    void Shutdown();

    //每帧更新 AOT 游戏模块。
    void Update(World& world, float deltaTime) override;

    //绘制 AOT 游戏模块 GUI。
    void DrawOverlay() override;

    //判断 AOT 游戏模块是否可用。
    bool IsInitialized() const;
};
