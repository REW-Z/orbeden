#pragma once

#include <chrono>
#include <string>

#include "Runtime/World.h"

class IEngineSystem
{
public:
    virtual ~IEngineSystem() = default;

    //固定步长更新，由 Application 按 fixedDeltaTime 补帧调用
    virtual void FixedUpdate(World& world, float fixedDeltaTime) {}

    //每帧更新，由 Application 每次 Tick 调用一次
    virtual void Update(World& world, float deltaTime) {}
};

class Application
{
private:
    World world;
    List<IEngineSystem*> systems;
    bool initialized = false;
    bool running = false;
    bool quitRequested = false;
    bool paused = false;
    uint32 targetFrameRate = 60;
    float fixedDeltaTime = 0.02f;
    uint32 maxFixedStepsPerFrame = 5;
    float fixedAccumulator = 0.0f;

public:
    Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    //释放应用持有的运行时状态
    ~Application();

    //初始化反射元数据并设置当前 World
    bool Initialize();

    //注册参与 Update/FixedUpdate 的运行时系统
    void RegisterSystem(IEngineSystem* system);

    //移除运行时系统
    void UnregisterSystem(IEngineSystem* system);

    //从 XML 文件读取 World，失败时保留空 World 继续运行
    bool LoadWorld(const std::string& path);

    //将当前 World 写入 XML 文件
    bool SaveWorld(const std::string& path) const;

    //推进一帧应用逻辑，包含 FixedUpdate 补帧和 Update
    void Tick(float deltaTime);

    //进入主循环，直到请求退出
    void Run();

    //请求主循环在当前帧后退出
    void RequestQuit();

    //退出应用并解除当前 World
    void Quit();

    //判断应用主循环是否仍在运行
    bool IsRunning() const;

    //判断应用是否暂停 Gameplay 更新
    bool IsPaused() const;

    //设置暂停状态，暂停时仍保留 BeginFrame/EndFrame
    void SetPaused(bool value);

    //获取应用持有的 World
    World& GetWorld();

    //获取应用持有的只读 World
    const World& GetWorld() const;

    //获取固定更新步长
    float GetFixedDeltaTime() const;

    //设置固定更新步长，非法值会被忽略
    void SetFixedDeltaTime(float value);

    //获取目标帧率，0 表示不限帧
    uint32 GetTargetFrameRate() const;

    //设置目标帧率，0 表示不限帧
    void SetTargetFrameRate(uint32 value);

    //获取单帧最多补跑 FixedUpdate 的次数
    uint32 GetMaxFixedStepsPerFrame() const;

    //设置单帧最多补跑 FixedUpdate 的次数
    void SetMaxFixedStepsPerFrame(uint32 value);

protected:
    //帧开始钩子，未来窗口/input 可以在这里接入
    virtual void BeginFrame() {}

    //帧结束钩子，未来渲染/present 可以在这里接入
    virtual void EndFrame() {}

    //判断主循环是否继续运行
    virtual bool ShouldKeepRunning() const;
};
