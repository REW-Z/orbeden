#include "Application.h"
#include "Editor/EditorSystem.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Platform/GlfwWindow.h"
#include "Platform/InputManager.h"

#include <chrono>

int main(int argc, char** argv)
{
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
        EditorSystem editorSystem(app, argc > 0 ? argv[0] : "");
        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();

        while (!window.ShouldClose())
        {
            bool continuousRepaint = editorSystem.NeedsContinuousRepaint();
            bool repaintRequested = editorSystem.TakeRepaintRequest();
            bool waitedForEvent = !continuousRepaint && !repaintRequested;

            //清理瞬时输入后按当前重绘状态轮询或阻塞等待事件
            InputManager::BeginFrame();
            if (waitedForEvent) window.WaitEvents();
            else window.PollEvents();
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
            if (continuousRepaint) app.WaitForNextFrame(frameStartTime);
        }
    }
    app.Quit();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
