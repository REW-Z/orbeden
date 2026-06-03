#include "Application.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Platform/GlfwWindow.h"
#include "Profiler/Profiler.h"

int main()
{
    GlfwWindow window;
    WindowDesc windowDesc;
    windowDesc.graphicsApi = WindowGraphicsApi::None;

    if (!window.Create(windowDesc))
    {
        Log::Error("Application startup failed: window create failed.");
        return 1;
    }

    Application app;
    app.SetWindow(&window);
    app.Initialize();
    app.LoadWorld("Worlds/default.world");
    app.Run();

    Profiler::WriteProfileLog();
    Profiler::Clear();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
