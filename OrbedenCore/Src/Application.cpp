#include <algorithm>
#include <thread>

#include "Application.h"
#include "Log/Log.h"
#include "Runtime/Reflection.h"
#include "Runtime/WorldSerializer.h"

//释放应用持有的运行时状态
Application::~Application()
{
    Quit();
}

//初始化反射元数据并设置当前 World
bool Application::Initialize()
{
    if (initialized) return true;

    //注册生成的反射信息，并建立兼容旧静态入口的当前 World
    Reflection::RegisterGeneratedReflection();
    World::SetCurrentWorld(&world);
    initialized = true;
    return true;
}

//注册参与 Update/FixedUpdate 的运行时系统
void Application::RegisterSystem(IEngineSystem* system)
{
    if (!system) return;

    //避免重复注册同一个系统实例
    auto it = std::find(systems.begin(), systems.end(), system);
    if (it != systems.end()) return;

    systems.push_back(system);
}

//移除运行时系统
void Application::UnregisterSystem(IEngineSystem* system)
{
    systems.erase(std::remove(systems.begin(), systems.end(), system), systems.end());
}

//从 XML 文件读取 World，失败时保留空 World 继续运行
bool Application::LoadWorld(const std::string& path)
{
    Initialize();

    //读取成功时直接使用反序列化后的 World
    if (WorldSerializer::LoadXml(world, path))
    {
        return true;
    }

    //启动阶段允许关卡缺失，保持开发期可运行
    Log::Warning("World load failed, continuing with empty world.");
    world.Clear();
    return false;
}

//将当前 World 写入 XML 文件
bool Application::SaveWorld(const std::string& path) const
{
    return WorldSerializer::SaveXml(world, path);
}

//推进一帧应用逻辑，包含 FixedUpdate 补帧和 Update
void Application::Tick(float deltaTime)
{
    Initialize();
    BeginFrame();

    //防御外部宿主传入异常时间
    if (deltaTime < 0.0f)
    {
        deltaTime = 0.0f;
    }

    //暂停时仍保留帧钩子，只跳过 Gameplay 更新
    if (!paused)
    {
        fixedAccumulator += deltaTime;

        //按固定步长补跑 FixedUpdate，限制单帧最大补帧次数
        uint32 fixedStepCount = 0;
        while (fixedAccumulator >= fixedDeltaTime && fixedStepCount < maxFixedStepsPerFrame)
        {
            for (IEngineSystem* system : systems)
            {
                if (system) system->FixedUpdate(world, fixedDeltaTime);
            }

            fixedAccumulator -= fixedDeltaTime;
            fixedStepCount++;
        }

        //过慢帧直接丢弃剩余积累，避免长时间追帧
        if (fixedStepCount == maxFixedStepsPerFrame && fixedAccumulator >= fixedDeltaTime)
        {
            fixedAccumulator = 0.0f;
        }

        //每帧调用一次普通 Update
        for (IEngineSystem* system : systems)
        {
            if (system) system->Update(world, deltaTime);
        }
    }
    else
    {
        fixedAccumulator = 0.0f;
    }

    EndFrame();
}

//进入主循环，直到请求退出
void Application::Run()
{
    Initialize();

    running = true;
    quitRequested = false;

    using Clock = std::chrono::steady_clock;
    auto previousTime = Clock::now();

    while (ShouldKeepRunning())
    {
        //计算本帧真实 deltaTime
        auto frameStartTime = Clock::now();
        auto currentTime = Clock::now();
        std::chrono::duration<float> elapsed = currentTime - previousTime;
        previousTime = currentTime;

        Tick(elapsed.count());

        //没有窗口垂直同步时，使用简单限帧避免空转烧满 CPU
        if (targetFrameRate > 0)
        {
            std::chrono::duration<float> targetFrameTime(1.0f / static_cast<float>(targetFrameRate));
            std::chrono::duration<float> usedFrameTime = Clock::now() - frameStartTime;
            if (usedFrameTime < targetFrameTime)
            {
                std::this_thread::sleep_for(targetFrameTime - usedFrameTime);
            }
        }
        else
        {
            std::this_thread::yield();
        }
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
    if (World::CurrentWorld() == &world)
    {
        World::SetCurrentWorld(nullptr);
    }

    running = false;
    quitRequested = true;
}

//判断应用主循环是否仍在运行
bool Application::IsRunning() const
{
    return running && !quitRequested;
}

//判断应用是否暂停 Gameplay 更新
bool Application::IsPaused() const
{
    return paused;
}

//设置暂停状态
void Application::SetPaused(bool value)
{
    paused = value;
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

//设置固定更新步长，非法值会被忽略
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

//设置目标帧率，0 表示不限帧
void Application::SetTargetFrameRate(uint32 value)
{
    targetFrameRate = value;
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

//判断主循环是否继续运行
bool Application::ShouldKeepRunning() const
{
    return running && !quitRequested;
}
