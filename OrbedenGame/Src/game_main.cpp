#include "Application.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Platform/GlfwWindow.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#if !defined(ORBEDEN_PROJECT_DIR)
#error ORBEDEN_PROJECT_DIR must identify the project packaged with this player.
#endif

namespace
{
    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return Utf8Path::ToUtf8(path.lexically_normal());
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        std::ostringstream output;
        output << input.rdbuf();
        return output.str();
    }

    std::string GetAttribute(const std::string& text, const std::string& name)
    {
        std::string token = name + "=\"";
        std::size_t start = text.find(token);
        if (start == std::string::npos) return std::string();

        start += token.size();
        std::size_t end = text.find('"', start);
        return end == std::string::npos ? std::string() : text.substr(start, end - start);
    }

    std::filesystem::path FindProjectFile(const std::filesystem::path& projectRoot)
    {
        std::filesystem::path expected = projectRoot / (projectRoot.filename().string() + ".oeproj");
        if (std::filesystem::is_regular_file(expected)) return expected;

        std::filesystem::path found;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(projectRoot))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".oeproj") continue;
            if (!found.empty())
            {
                Log::Error(("Player project contains multiple .oeproj files: " + ToCleanPath(projectRoot)).c_str());
                return std::filesystem::path();
            }

            found = entry.path();
        }

        return found;
    }

    bool LoadConfiguredProject(Application& app)
    {
        std::filesystem::path projectRoot = Utf8Path::FromUtf8(ORBEDEN_PROJECT_DIR);
        if (!std::filesystem::is_directory(projectRoot))
        {
            Log::Error(("Player project directory does not exist: " + ToCleanPath(projectRoot)).c_str());
            return false;
        }

        std::filesystem::path projectFile = FindProjectFile(projectRoot);
        if (projectFile.empty())
        {
            Log::Error(("Player project file was not found: " + ToCleanPath(projectRoot)).c_str());
            return false;
        }

        std::string content = ReadTextFile(projectFile);
        std::string startupWorld = GetAttribute(content, "startupWorld");
        if (startupWorld.empty())
        {
            Log::Error(("Player project is missing startupWorld: " + ToCleanPath(projectFile)).c_str());
            return false;
        }

        std::string resourceRoot = GetAttribute(content, "resourceRoot");
        PathDefines::SetContentRoot(ToCleanPath(projectRoot), resourceRoot.empty() ? "Resource" : resourceRoot);
        std::string worldPath = ToCleanPath(projectRoot / Utf8Path::FromUtf8(startupWorld));
        if (app.LoadWorld(worldPath)) return true;

        Log::Error(("Player startup world load failed: " + worldPath).c_str());
        return false;
    }
}

int main()
{
    GlfwWindow window;
    WindowDesc windowDesc;
    windowDesc.graphicsApi = WindowGraphicsApi::OpenGL;

    if (!window.Create(windowDesc))
    {
        Log::Error("Game startup failed: window create failed.");
        return 1;
    }

    Application app(ScriptRuntimeMode::AOT);
    app.SetWindow(&window);
    app.SetTargetFrameRate(60);
    if (!app.Initialize())
    {
        Log::Error("Game startup failed: application initialize failed.");
        return 1;
    }

    if (!LoadConfiguredProject(app))
    {
        app.Quit();
        return 1;
    }

    app.Run();
    app.Quit();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
