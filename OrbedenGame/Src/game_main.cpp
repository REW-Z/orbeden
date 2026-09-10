#include "Application.h"
#include "Log/Log.h"
#include "Memory/MemoryManager.h"
#include "Platform/GlfwWindow.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "Scripting/ScriptSystem.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#if defined(ORBEDEN_HAS_NATIVE_GAME)
extern "C" void OrbedenGameNative_RegisterReflection();
#endif

//游戏 C# 程序集 NativeAOT 导出的脚本入口；只有 Player 链接这些符号。
//Core 是底层 SDK，不引用用户层符号，因此入口由本层运行时注入。
extern "C"
{
    void ORBEDEN_NATIVE_CALL OrbedenGame_Initialize(void* nativeApi);
    void ORBEDEN_NATIVE_CALL OrbedenGame_Shutdown();
    void ORBEDEN_NATIVE_CALL OrbedenGame_Update(float32 deltaTime);
    void ORBEDEN_NATIVE_CALL OrbedenGame_FixedUpdate(float32 deltaTime);
    void ORBEDEN_NATIVE_CALL OrbedenGame_LateUpdate(float32 deltaTime);
    void ORBEDEN_NATIVE_CALL OrbedenGame_EnsWorldActiveChanged(EnsId ens, uint8 worldActive);
    void ORBEDEN_NATIVE_CALL OrbedenGame_EnsDestroyed(EnsId ens);
    void ORBEDEN_NATIVE_CALL OrbedenGame_DrawGui();
}

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

#if defined(ORBEDEN_HAS_NATIVE_GAME)
    OrbedenGameNative_RegisterReflection();
#endif

    if (!LoadConfiguredProject(app))
    {
        app.Quit();
        return 1;
    }

    ScriptSystem* scriptSystem = app.GetSystem<ScriptSystem>();
    if (!scriptSystem)
    {
        Log::Error("Game startup failed: script system was not found.");
        app.Quit();
        return 1;
    }

    //把本层链接的 AOT 导出入口注入 Core，再启动脚本域。
    ScriptEntryPoints aotEntryPoints;
    aotEntryPoints.initialize = &OrbedenGame_Initialize;
    aotEntryPoints.shutdown = &OrbedenGame_Shutdown;
    aotEntryPoints.update = &OrbedenGame_Update;
    aotEntryPoints.fixedUpdate = &OrbedenGame_FixedUpdate;
    aotEntryPoints.lateUpdate = &OrbedenGame_LateUpdate;
    aotEntryPoints.ensWorldActiveChanged = &OrbedenGame_EnsWorldActiveChanged;
    aotEntryPoints.ensDestroyed = &OrbedenGame_EnsDestroyed;
    aotEntryPoints.drawGui = &OrbedenGame_DrawGui;
    if (!scriptSystem->SetAotEntryPoints(aotEntryPoints) || !scriptSystem->Initialize())
    {
        Log::Error("Game startup failed: script domains could not initialize.");
        app.Quit();
        return 1;
    }

    app.Run();
    app.Quit();

    Memory::GetHeapAllocator()->Analysis();

    return 0;
}
