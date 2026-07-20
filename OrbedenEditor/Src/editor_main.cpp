#include "Application.h"
#include "Editor/EditorSystem.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Platform/GlfwWindow.h"

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
        app.Run([&editorSystem](World& world, float deltaTime)
            {
                editorSystem.Update(world, deltaTime);
            });
    }
    app.Quit();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
