#include "Application.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Platform/GlfwWindow.h"
#include "Profiler/Profiler.h"
#include "Runtime/Managed/ManagedRuntimeOverlay.h"
#include "Runtime/Managed/ScriptSystem.h"
#include "Runtime/ContentContext.h"

#include <filesystem>
#include <string>

namespace
{
    std::filesystem::path GetExecutableDirectory(const char* executablePath)
    {
        if (!executablePath || executablePath[0] == '\0') return std::filesystem::current_path();

        std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(executablePath));
        return path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    }
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

    std::string exampleProject = ContentContext::FindProjectRoot("ExampleProject", argc > 0 ? argv[0] : "");
    if (!exampleProject.empty())
    {
        ContentContext::SetContentRoot(exampleProject, "Resource");
        std::string worldPath = (std::filesystem::path(exampleProject) / "World/example_world.world").lexically_normal().generic_string();
        app.LoadWorld(worldPath);
    }
    else
    {
        Log::Warning("Game startup warning: ExampleProject was not found.");
    }

    std::filesystem::path executableDirectory = GetExecutableDirectory(argc > 0 ? argv[0] : "");
    std::filesystem::path managedDirectory = executableDirectory / "Managed";

    ScriptSystem scriptSystem;
    ScriptSystemConfig scriptConfig;
    scriptConfig.runtimeConfigPath = (executableDirectory / "OrbedenCore.runtimeconfig.json").lexically_normal().generic_string();
    scriptConfig.managedDirectory = exampleProject.empty()
        ? managedDirectory.lexically_normal().generic_string()
        : (std::filesystem::path(exampleProject) / "Managed").lexically_normal().generic_string();
    scriptConfig.runtimeAssemblyPath = (managedDirectory / "Orbeden.Runtime.dll").lexically_normal().generic_string();
    scriptConfig.componentAssemblyPath = scriptConfig.runtimeAssemblyPath;
    if (scriptSystem.Initialize(scriptConfig))
    {
        app.RegisterSystem(&scriptSystem);
    }

    ManagedRuntimeOverlay managedOverlay;
    ManagedRuntimeOverlayConfig managedConfig;
    managedConfig.userAssemblyPath = exampleProject.empty()
        ? (managedDirectory / "ExampleGame.dll").lexically_normal().generic_string()
        : (std::filesystem::path(exampleProject) / "Managed" / "ExampleGame.dll").lexically_normal().generic_string();
    managedConfig.userTypeName = "ExampleGame.GuiOverlay, ExampleGame";
    managedConfig.userMethodName = "OnGui";
    if (managedOverlay.Initialize(scriptSystem, managedConfig))
    {
        if (RenderSystem* renderSystem = app.GetRenderSystem())
        {
            renderSystem->SetRenderOverlay(&managedOverlay);
        }
    }

    app.Run();
    managedOverlay.Shutdown();
    app.UnregisterSystem(&scriptSystem);
    scriptSystem.Shutdown();

    Profiler::WriteProfileLog();
    Profiler::Clear();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
