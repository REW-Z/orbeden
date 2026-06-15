#include "Application.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Platform/GlfwWindow.h"
#include "Profiler/Profiler.h"
#include "Runtime/ProjectContext.h"

#include <filesystem>
#include <string>

namespace examples
{
    void InitializeExampleWorldRuntime(Application& app);
}

int main(int argc, char** argv)
{
    GlfwWindow window;
    WindowDesc windowDesc;
    windowDesc.graphicsApi = WindowGraphicsApi::OpenGL;

    if (!window.Create(windowDesc))
    {
        Log::Error("Game startup failed: window create failed.");
        return 1;
    }

    Application app;
    app.SetWindow(&window);
    app.SetTargetFrameRate(60);
    if (!app.Initialize())
    {
        Log::Error("Game startup failed: application initialize failed.");
        return 1;
    }

    std::string exampleProject = ProjectContext::FindProjectRoot("ExampleProject", argc > 0 ? argv[0] : "");
    if (!exampleProject.empty())
    {
        ProjectContext::SetProjectRoot(exampleProject);
        std::string worldPath = (std::filesystem::path(exampleProject) / "Worlds/example_world.world").lexically_normal().generic_string();
        app.LoadWorld(worldPath);
    }
    else
    {
        Log::Warning("Game startup warning: ExampleProject was not found.");
    }

    examples::InitializeExampleWorldRuntime(app);
    app.Run();

    Profiler::WriteProfileLog();
    Profiler::Clear();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
