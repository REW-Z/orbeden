#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>

#include "Platform/Window.h"
#include "Runtime/World.h"

class Application;

class IEngineSystem
{
public:
    virtual ~IEngineSystem() = default;

    //系统首次创建时调用，可通过 Application 获取依赖系统
    virtual bool OnInitialize(Application& app) { return true; }

    //固定步长更新，由 Application 按 fixedDeltaTime 补帧调用
    virtual void FixedUpdate(World& world, float fixedDeltaTime) {}

    //每帧更新，由 Application 每次 Tick 调用一次
    virtual void Update(World& world, float deltaTime) {}

    //窗口 framebuffer 尺寸变化时调用
    virtual void OnWindowResize(int width, int height) {}

    //Application 退出时按创建顺序的逆序调用
    virtual void OnShutdown() {}
};

//脚本运行时入口来源
enum class ScriptRuntimeMode
{
    AOT,
    CLR,
};

class Application : public IWindowResizeListener
{
private:
    struct EngineSystemEntry
    {
    public:
        std::type_index type;
        std::unique_ptr<IEngineSystem> system;

        //保存系统类型和由 Application 拥有的实例
        EngineSystemEntry(std::type_index systemType, std::unique_ptr<IEngineSystem> systemInstance)
            : type(systemType)
            , system(std::move(systemInstance))
        {
        }
    };

    World world;
    IWindow* window = nullptr;
    List<EngineSystemEntry> systems;
    List<std::type_index> initializingSystems;
    ScriptRuntimeMode scriptRuntimeMode;
    bool initialized = false;
    bool shuttingDown = false;
    bool running = false;
    bool quitRequested = false;
    bool paused = false;
    bool simulationEnabled = true;
    uint32 targetFrameRate = 60;
    float fixedDeltaTime = 0.02f;
    uint32 maxFixedStepsPerFrame = 5;
    float fixedAccumulator = 0.0f;

public:
    //创建使用指定脚本运行时的应用
    explicit Application(ScriptRuntimeMode mode);
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    //释放应用持有的运行时状态
    ~Application();

    //初始化反射元数据、当前 World 和全部内置系统
    bool Initialize();

    //获取系统，尚未创建时立即创建并初始化
    template<typename T>
    T* GetSystem();

    //从 XML 文件读取 World，失败时保留空 World 继续运行
    bool LoadWorld(const std::string& path);

    //将当前 World 写入 XML 文件
    bool SaveWorld(const std::string& path) const;

    //推进一帧 Simulation 逻辑
    void Tick(float deltaTime);

    //渲染当前 World
    void Render(float deltaTime);

    //提交窗口显示
    void Present();

    //进入主循环，直到请求退出
    void Run();

    //请求主循环在当前帧后退出
    void RequestQuit();

    //退出应用并解除当前 World
    void Quit();

    //判断应用主循环是否仍在运行
    bool IsRunning() const;

    //判断应用是否暂停 Simulation 更新
    bool IsPaused() const;

    //设置暂停状态，暂停时仍保留帧生命周期
    void SetPaused(bool value);

    //判断 Simulation 更新是否启用
    bool IsSimulationEnabled() const;

    //设置 Simulation 更新是否启用
    void SetSimulationEnabled(bool value);

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

    //等待到当前目标帧的结束时间
    void WaitForNextFrame(std::chrono::steady_clock::time_point frameStartTime) const;

    //获取单帧最多补跑 FixedUpdate 的次数
    uint32 GetMaxFixedStepsPerFrame() const;

    //设置单帧最多补跑 FixedUpdate 的次数
    void SetMaxFixedStepsPerFrame(uint32 value);

    //绑定外部创建的窗口，Application 不拥有窗口生命周期
    void SetWindow(IWindow* newWindow);

    //获取当前绑定窗口
    IWindow* GetWindow() const;

    //获取脚本运行时入口来源
    ScriptRuntimeMode GetScriptRuntimeMode() const;

    //派发窗口 resize 到已创建系统
    void OnWindowResize(int32 width, int32 height) override;

private:
    //查找已经创建的系统
    IEngineSystem* FindSystem(std::type_index type) const;

    //接收新系统并执行统一初始化
    IEngineSystem* CreateSystem(std::type_index type, std::unique_ptr<IEngineSystem> system);

    //逆序关闭并销毁全部系统
    void ShutdownSystems();

    //判断主循环是否继续运行
    bool ShouldKeepRunning() const;
};

//获取系统，尚未创建时立即创建并初始化
template<typename T>
T* Application::GetSystem()
{
    static_assert(std::is_base_of_v<IEngineSystem, T>);
    static_assert(std::is_default_constructible_v<T>);

    std::type_index type = typeid(T);
    if (IEngineSystem* system = FindSystem(type))
    {
        return static_cast<T*>(system);
    }

    return static_cast<T*>(CreateSystem(type, std::make_unique<T>()));
}
