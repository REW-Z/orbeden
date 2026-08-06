#include <algorithm>
#include <chrono>
#include <thread>

#include "Application.h"
#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Physics/PhysicsReflection.h"
#include "Physics/PhysicsSystem.h"
#include "Platform/InputManager.h"
#include "Profiler/Profiler.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/Object/Object.h"
#include "Runtime/Reflection.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/WorldSerializer.h"
#include "Scripting/ScriptSystem.h"

namespace
{
    std::chrono::steady_clock::duration CalculateFrameTimeTolerance(uint32 targetFrameRate)
    {
        if (targetFrameRate == 0) return std::chrono::steady_clock::duration::zero();

        double targetFrameSeconds = 1.0 / static_cast<double>(targetFrameRate);
        double acceptedFastFrameSeconds = 1.0 / static_cast<double>(targetFrameRate + 1);
        return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(targetFrameSeconds - acceptedFastFrameSeconds));
    }

    void WaitUntilFrameTime(std::chrono::steady_clock::time_point targetTime, uint32 targetFrameRate)
    {
        using Clock = std::chrono::steady_clock;
        using Microseconds = std::chrono::microseconds;
        constexpr auto shortSleep = std::chrono::milliseconds(1);
        constexpr auto sleepPadding = std::chrono::microseconds(100);
        static int64 estimatedSleepUs = 1000;

        auto acceptedTime = targetTime - CalculateFrameTimeTolerance(targetFrameRate);
        while (Clock::now() < acceptedTime)
        {
            auto remaining = acceptedTime - Clock::now();
            auto sleepGuard = Microseconds(estimatedSleepUs) + sleepPadding;
            if (remaining > sleepGuard)
            {
                auto sleepStart = Clock::now();
                std::this_thread::sleep_for(shortSleep);
                auto sleepEnd = Clock::now();

                int64 sleptUs = std::chrono::duration_cast<Microseconds>(sleepEnd - sleepStart).count();
                if (sleptUs > estimatedSleepUs)
                {
                    estimatedSleepUs = sleptUs;
                }
                else
                {
                    estimatedSleepUs = (estimatedSleepUs * 7 + sleptUs) / 8;
                }
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }
}

//创建使用指定脚本运行时的应用
Application::Application(ScriptRuntimeMode mode)
    : scriptRuntimeMode(mode)
{
}

//释放应用持有的运行时状态
Application::~Application()
{
    Quit();
}

//初始化反射元数据、当前 World 和全部内置系统
bool Application::Initialize()
{
    if (initialized) return true;

    //注册反射信息并绑定当前 World
    Reflection::RegisterGeneratedReflection();
    PhysicsReflection::Register();
    World::SetCurrentWorld(&world);

    //创建内置系统
    if (!GetSystem<Profiler>()
        || !GetSystem<FileSystem>()
        || !GetSystem<InputManager>()
        || !GetSystem<ResourceManager>()
        || !GetSystem<PhysicsSystem>()
        || !GetSystem<RenderSystem>()
        || !GetSystem<ScriptSystem>())
    {
        ShutdownSystems();
        World::SetCurrentWorld(nullptr);
        return false;
    }

    initialized = true;
    return true;
}

//查找已经创建的系统
IEngineSystem* Application::FindSystem(std::type_index type) const
{
    for (const EngineSystemEntry& entry : systems)
    {
        if (entry.type == type) return entry.system.get();
    }

    return nullptr;
}

//接收新系统并执行统一初始化
IEngineSystem* Application::CreateSystem(std::type_index type, std::unique_ptr<IEngineSystem> system)
{
    if (shuttingDown)
    {
        Log::Error(("Cannot create engine system during shutdown: " + std::string(type.name())).c_str());
        return nullptr;
    }

    if (std::find(initializingSystems.begin(), initializingSystems.end(), type) != initializingSystems.end())
    {
        Log::Error(("Circular engine system dependency: " + std::string(type.name())).c_str());
        return nullptr;
    }

    //初始化系统依赖
    initializingSystems.push_back(type);
    bool succeeded = system->OnInitialize(*this);
    initializingSystems.pop_back();
    if (!succeeded)
    {
        system->OnShutdown();
        Log::Error(("Engine system initialize failed: " + std::string(type.name())).c_str());
        return nullptr;
    }

    IEngineSystem* result = system.get();
    systems.emplace_back(type, std::move(system));
    return result;
}

//逆序关闭并销毁全部系统
void Application::ShutdownSystems()
{
    shuttingDown = true;
    while (!systems.empty())
    {
        systems.back().system->OnShutdown();
        systems.pop_back();
    }
    initializingSystems.clear();
    shuttingDown = false;
}

//从 XML 文件读取 World
bool Application::LoadWorld(const std::string& path)
{
    if (!Initialize()) return false;
    if (PhysicsSystem* physicsSystem = GetSystem<PhysicsSystem>()) physicsSystem->ResetWorld();

    //替换当前 World
    if (WorldSerializer::LoadXml(world, path))
    {
        return true;
    }

    //记录 World 读取失败
    Log::Warning("World load failed, continuing with empty world.");
    world.Clear();
    return false;
}

//将当前 World 写入 XML 文件
bool Application::SaveWorld(const std::string& path) const
{
    return WorldSerializer::SaveXml(world, path);
}

//推进一帧 Simulation 逻辑
void Application::Tick(float deltaTime)
{
    if (!Initialize()) return;

    //校正帧时间
    if (deltaTime < 0.0f)
    {
        deltaTime = 0.0f;
    }

    //重置固定时间积累
    bool runSimulation = simulationEnabled && !paused;
    if(!runSimulation)
    {
        fixedAccumulator = 0.0f;
    }
    if (runSimulation)
    {
        fixedAccumulator += deltaTime;


        /// *** Fixed Update ***

        //补跑固定步长更新
        uint32 fixedStepCount = 0;
        while (fixedAccumulator >= fixedDeltaTime && fixedStepCount < maxFixedStepsPerFrame)
        {
            usize fixedSystemCount = systems.size();
            for (usize index = 0; index < fixedSystemCount; index++)
            {
                systems[index].system->FixedUpdate(world, fixedDeltaTime);
            }

            fixedAccumulator -= fixedDeltaTime;
            fixedStepCount++;
        }
        //丢弃剩余时间积累
        if (fixedStepCount == maxFixedStepsPerFrame && fixedAccumulator >= fixedDeltaTime)
        {
            fixedAccumulator = 0.0f;
        }


        /// *** Update ***

        usize updateSystemCount = systems.size();
        for (usize index = 0; index < updateSystemCount; index++)
        {
            systems[index].system->Update(world, deltaTime);
        }
    }
}

//渲染当前 World
void Application::Render(float deltaTime)
{
    if (!Initialize() || !window) return;

    RenderSystem* renderSystem = GetSystem<RenderSystem>();
    if (!renderSystem) return;

    //渲染系统
    renderSystem->Render(world, deltaTime);
}

//提交窗口显示
void Application::Present()
{
    if (!window) return;
    window->Present();
}

//运行应用主循环
void Application::Run()
{
    if (!Initialize()) return;

    running = true;
    quitRequested = false;

    using Clock = std::chrono::steady_clock;
    auto previousTime = Clock::now();

    //GameLoop
    while (ShouldKeepRunning())
    {
        //更新本帧输入状态
        InputManager::BeginFrame();
        window->PollEvents();
        if (window->ShouldClose())
        {
            RequestQuit();
            break;
        }

        //计算本帧时间
        auto frameStartTime = Clock::now();
        std::chrono::duration<float> elapsed = frameStartTime - previousTime;
        previousTime = frameStartTime;

        //各系统更新推进
        Tick(elapsed.count());

        //渲染
        Render(elapsed.count());
        Present();

        WaitForNextFrame(frameStartTime);
    }

    running = false;
}

//请求主循环在当前帧后退出
void Application::RequestQuit()
{
    quitRequested = true;
}

//退出应用并解除当前 World
void Application::Quit()
{
    running = false;
    quitRequested = true;
    initialized = false;

    ShutdownSystems();
    SetWindow(nullptr);
    Object::UnloadUnusedObjects(nullptr, 0);
    world.Clear();
    Object::ReleaseOrphanInstances();

    if (World::CurrentWorld() == &world)
    {
        World::SetCurrentWorld(nullptr);
    }
}

//判断应用主循环是否仍在运行
bool Application::IsRunning() const
{
    return running && !quitRequested;
}

//判断应用是否暂停 Simulation 更新
bool Application::IsPaused() const
{
    return paused;
}

//设置暂停状态
void Application::SetPaused(bool value)
{
    paused = value;
}

//判断 Simulation 更新是否启用
bool Application::IsSimulationEnabled() const
{
    return simulationEnabled;
}

//设置 Simulation 更新是否启用
void Application::SetSimulationEnabled(bool value)
{
    simulationEnabled = value;
    if (!simulationEnabled)
    {
        fixedAccumulator = 0.0f;
    }
}

//获取应用持有的 World
World& Application::GetWorld()
{
    return world;
}

//获取应用持有的只读 World
const World& Application::GetWorld() const
{
    return world;
}

//获取固定更新步长
float Application::GetFixedDeltaTime() const
{
    return fixedDeltaTime;
}

//设置固定更新步长
void Application::SetFixedDeltaTime(float value)
{
    if (value <= 0.0f) return;

    fixedDeltaTime = value;
}

//获取目标帧率
uint32 Application::GetTargetFrameRate() const
{
    return targetFrameRate;
}

//设置目标帧率
void Application::SetTargetFrameRate(uint32 value)
{
    targetFrameRate = value;
}

//等待到当前目标帧的结束时间
void Application::WaitForNextFrame(std::chrono::steady_clock::time_point frameStartTime) const
{
    if (targetFrameRate == 0)
    {
        std::this_thread::yield();
        return;
    }

    std::chrono::duration<float> targetFrameTime(1.0f / static_cast<float>(targetFrameRate));
    auto targetEndTime = frameStartTime
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(targetFrameTime);
    WaitUntilFrameTime(targetEndTime, targetFrameRate);
}

//获取单帧最多补跑 FixedUpdate 的次数
uint32 Application::GetMaxFixedStepsPerFrame() const
{
    return maxFixedStepsPerFrame;
}

//设置单帧最多补跑 FixedUpdate 的次数
void Application::SetMaxFixedStepsPerFrame(uint32 value)
{
    if (value == 0) return;

    maxFixedStepsPerFrame = value;
}

//绑定外部创建的窗口
void Application::SetWindow(IWindow* newWindow)
{
    if (window == newWindow) return;

    RenderSystem* renderSystem = initialized ? GetSystem<RenderSystem>() : nullptr;
    if (renderSystem) renderSystem->Shutdown();

    if (window)
    {
        window->SetResizeListener(nullptr);
    }

    window = newWindow;
    if (!window) return;

    window->SetResizeListener(this);
    OnWindowResize(window->GetFramebufferWidth(), window->GetFramebufferHeight());

    //重建窗口渲染后端
    if (renderSystem && (window->GetGraphicsApi() != WindowGraphicsApi::OpenGL || !renderSystem->Initialize(window)))
    {
        Log::Error("Application window does not provide a supported graphics API.");
        RequestQuit();
    }
}

//获取当前绑定窗口
IWindow* Application::GetWindow() const
{
    return window;
}

//获取脚本运行时入口来源
ScriptRuntimeMode Application::GetScriptRuntimeMode() const
{
    return scriptRuntimeMode;
}

//派发窗口 resize 到已创建系统
void Application::OnWindowResize(int32 width, int32 height)
{
    usize systemCount = systems.size();
    for (usize index = 0; index < systemCount; index++)
    {
        systems[index].system->OnWindowResize(width, height);
    }
}

//判断主循环是否继续运行
bool Application::ShouldKeepRunning() const
{
    return running && !quitRequested && window && !window->ShouldClose();
}
