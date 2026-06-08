#include "Application.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Platform/GlfwWindow.h"
#include "Profiler/Profiler.h"

namespace examples
{
    void ExampleRenderScene(World& world);
}

int main()
{
    GlfwWindow window;
    WindowDesc windowDesc;
    windowDesc.graphicsApi = WindowGraphicsApi::OpenGL;

    if (!window.Create(windowDesc))
    {
        Log::Error("Application startup failed: window create failed.");
        return 1;
    }

    Application app;
    app.SetWindow(&window);
    if (!app.Initialize())
    {
        Log::Error("Application startup failed: application initialize failed.");
        return 1;
    }

    app.LoadWorld("Worlds/default.world");
    examples::ExampleRenderScene(app.GetWorld());
    app.Run();

    Profiler::WriteProfileLog();
    Profiler::Clear();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
