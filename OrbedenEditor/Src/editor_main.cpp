#include "Application.h"
#include "Editor/EditorSystem.h"
#include "Editor/AssetInspection.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "ResourceManager/ResourceManager.h"
#include <filesystem>
#include <fstream>
#include <string_view>
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Profiler/Profiler.h"
#include "Platform/GlfwWindow.h"
#include "InputManager/InputManager.h"

#include <chrono>

int wmain(int argc, wchar_t** argv)
{
    //资源检查工作进程不初始化图形或编辑器，只运行真实导入器并写出轻量快照。
    if (argc == 5 && std::wstring_view(argv[1]) == L"--inspect-asset")
    {
        PathDefines::SetContentRoot(Utf8Path::ToUtf8(std::filesystem::path(argv[2])));
        std::filesystem::path directory(argv[4]);
        std::filesystem::create_directories(directory);
        std::string result = AssetInspection::Inspect(Utf8Path::ToUtf8(std::filesystem::path(argv[3])), Utf8Path::ToUtf8(directory));
        std::ofstream output(directory / "inspection.result", std::ios::binary | std::ios::trunc);
        output.write(result.data(), static_cast<std::streamsize>(result.size()));
        output.flush();
        bool succeeded = output.good();
        ResourceManager::Shutdown();
        return succeeded ? 0 : 1;
    }
    GlfwWindow window;
    WindowDesc windowDesc;
    windowDesc.title = "Orbeden Editor";
    windowDesc.graphicsApi = WindowGraphicsApi::OpenGL;

    if (!window.Create(windowDesc))
    {
        Log::Error("Editor startup failed: window create failed.");
        return 1;
    }

    Application app(ScriptRuntimeMode::CLR);
    app.SetWindow(&window);
    app.SetTargetFrameRate(60);
    app.SetSimulationEnabled(false);
    if (!app.Initialize())
    {
        Log::Error("Editor startup failed: application initialize failed.");
        return 1;
    }

    {
        std::string executable = argc > 0 ? Utf8Path::ToUtf8(std::filesystem::path(argv[0])) : "";
        EditorSystem editorSystem(app, executable.c_str());

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();

        //请求过重绘的面板很可能还会周期性再请求。完全阻塞的等待会让「每秒刷新」的面板
        //再也醒不过来，所以请求过后留一段宽限期，期间按秒超时唤醒；宽限期过后回到完全阻塞。
        constexpr double IdleWakeSeconds = 1.0;
        constexpr int32 IdleWakeGraceMilliseconds = 2100;
        auto idleWakeDeadline = previousTime;

        while (!window.ShouldClose())
        {
            bool continuousRepaint = editorSystem.NeedsContinuousRepaint();
            bool repaintRequested = editorSystem.TakeRepaintRequest();
            bool waitedForEvent = !continuousRepaint && !repaintRequested;
            if (repaintRequested) idleWakeDeadline = Clock::now() + std::chrono::milliseconds(IdleWakeGraceMilliseconds);

            //清理瞬时输入后按当前重绘状态轮询、限时等待或阻塞等待事件
            InputManager::BeginFrame();
            if (!waitedForEvent) window.PollEvents();
            else if (Clock::now() < idleWakeDeadline) window.WaitEventsTimeout(IdleWakeSeconds);
            else window.WaitEvents();
            if (window.ShouldClose()) break;

            //空闲唤醒帧不把等待时间传入 Simulation
            auto frameStartTime = Clock::now();
            float deltaTime = waitedForEvent
                ? 0.0f
                : std::chrono::duration<float>(frameStartTime - previousTime).count();
            previousTime = frameStartTime;

            app.Tick(deltaTime);
            editorSystem.Update(app.GetWorld(), deltaTime);
            app.Render(deltaTime);
            editorSystem.RenderEditorGUI();
            app.Present();

            //请求重绘的面板每帧都会再请求一次，这里必须按目标帧率等待，
            //否则会变成不受限的满速重绘
            if (continuousRepaint || repaintRequested) app.WaitForNextFrame(frameStartTime);

            //节拍等完再收帧：帧耗时是这一帧自己的跨度，不借用模拟的 delta
            Profiler::EndFrame();
        }
    }
    app.Quit();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
