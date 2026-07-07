#include "Application.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Platform/GlfwWindow.h"
#include "Profiler/Profiler.h"
#include "FileSystem/PathDefines.h"
#include "ScriptModule.h"

#include <filesystem>
#include <string>

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

    std::string exampleProject = PathDefines::FindProjectRoot("ExampleProject", argc > 0 ? argv[0] : "");
    if (!exampleProject.empty())
    {
        PathDefines::SetProjectRoot(exampleProject, "Resource");
        std::string worldPath = (std::filesystem::path(exampleProject) / "World/example_world.world").lexically_normal().generic_string();
        app.LoadWorld(worldPath);
    }
    else
    {
        Log::Warning("Game startup warning: ExampleProject was not found.");
    }

    ScriptModule scriptModule;
    if (scriptModule.Initialize())
    {
        app.RegisterSystem(&scriptModule);
        if (RenderSystem* renderSystem = app.GetRenderSystem())
        {
            renderSystem->SetRenderOverlay(&scriptModule);
        }
    }

    app.Run();
    app.UnregisterSystem(&scriptModule);
    scriptModule.Shutdown();

    Profiler::WriteProfileLog();
    Profiler::Clear();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
