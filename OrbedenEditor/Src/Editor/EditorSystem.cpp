#include "Editor/EditorSystem.h"

#include "Log/Log.h"
#include "Editor/NewProjectGenerator.h"
#include "Editor/Panels/EditorPanelRegistry.h"
#include "Platform/InputManager.h"
#include "FileSystem/Utf8Path.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include <sstream>
#include <utility>

namespace
{
    struct PlayerTargetPlatformInfo
    {
        const char* displayName;
        const char* scriptName;
        const char* cmakePreset;
        const char* cmakeBuildDirectory;
        const char* aotDirectory;
    };

    constexpr std::array<PlayerTargetPlatformInfo, 5> PlayerTargetPlatforms =
    {
        PlayerTargetPlatformInfo { "Windows x64", "WindowsX64", "player-windows-x64-clang-cl", "windows-x64-clang-cl", "windows-x64" },
        PlayerTargetPlatformInfo { "Linux x64", "LinuxX64", "player-linux-x64-clang", "linux-x64-clang", "linux-x64-clang" },
        PlayerTargetPlatformInfo { "Linux x64 GCC", "LinuxX64Gcc", "player-linux-x64-gcc", "linux-x64-gcc", "linux-x64-gcc" },
        PlayerTargetPlatformInfo { "FreeBSD x64", "FreeBsdX64", "player-freebsd-x64-clang", "freebsd-x64-clang", "freebsd-x64" },
        PlayerTargetPlatformInfo { "Switch", "Switch", "player-switch", "switch", "switch" },
    };

    enum class ToolbarIcon
    {
        Play,
        Pause,
        Stop,
    };

    //绘制不依赖字体字符集的工具栏图标按钮。
    bool DrawToolbarIconButton(const char* id, ToolbarIcon icon, const ImVec2& size)
    {
        bool clicked = ImGui::Button(id, size);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        float32 width = max.x - min.x;
        float32 height = max.y - min.y;
        float32 side = std::min(width, height);
        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

        if (icon == ToolbarIcon::Play)
        {
            float32 triangleWidth = side * 0.42f;
            float32 triangleHeight = side * 0.48f;
            drawList->AddTriangleFilled(
                ImVec2(center.x - triangleWidth * 0.38f, center.y - triangleHeight * 0.5f),
                ImVec2(center.x - triangleWidth * 0.38f, center.y + triangleHeight * 0.5f),
                ImVec2(center.x + triangleWidth * 0.52f, center.y),
                color);
        }
        else if (icon == ToolbarIcon::Pause)
        {
            float32 barWidth = side * 0.13f;
            float32 barHeight = side * 0.48f;
            float32 gap = side * 0.12f;
            drawList->AddRectFilled(
                ImVec2(center.x - gap * 0.5f - barWidth, center.y - barHeight * 0.5f),
                ImVec2(center.x - gap * 0.5f, center.y + barHeight * 0.5f),
                color,
                1.0f);
            drawList->AddRectFilled(
                ImVec2(center.x + gap * 0.5f, center.y - barHeight * 0.5f),
                ImVec2(center.x + gap * 0.5f + barWidth, center.y + barHeight * 0.5f),
                color,
                1.0f);
        }
        else
        {
            float32 squareSize = side * 0.42f;
            drawList->AddRectFilled(
                ImVec2(center.x - squareSize * 0.5f, center.y - squareSize * 0.5f),
                ImVec2(center.x + squareSize * 0.5f, center.y + squareSize * 0.5f),
                color,
                1.5f);
        }

        return clicked;
    }

    const PlayerTargetPlatformInfo& GetPlayerTargetPlatformInfo(int32 index)
    {
        if (index < 0 || index >= static_cast<int32>(PlayerTargetPlatforms.size()))
        {
            return PlayerTargetPlatforms[0];
        }

        return PlayerTargetPlatforms[static_cast<usize>(index)];
    }

    std::string GetNativeAotLibraryName(const PlayerTargetPlatformInfo& target, const std::string& assemblyName)
    {
        if (std::strcmp(target.scriptName, "WindowsX64") == 0)
        {
            return assemblyName + ".lib";
        }

        return "lib" + assemblyName + ".a";
    }

#if defined(NDEBUG)
    constexpr const char* BuildConfiguration = "Release";
#else
    constexpr const char* BuildConfiguration = "Debug";
#endif

    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return Utf8Path::ToUtf8(path.lexically_normal());
    }

    std::filesystem::path GetExecutableDirectory(const std::string& executablePath)
    {
        if (executablePath.empty()) return std::filesystem::current_path();

        std::filesystem::path path = std::filesystem::absolute(Utf8Path::FromUtf8(executablePath));
        return path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    }

    void CopyToBuffer(char* buffer, std::size_t bufferSize, const std::string& value)
    {
        if (!buffer || bufferSize == 0) return;

        std::size_t copySize = std::min(bufferSize - 1, value.size());
        std::memcpy(buffer, value.data(), copySize);
        buffer[copySize] = '\0';
    }

    List<std::string> GetChildDirectories(const std::string& directory)
    {
        List<std::string> result;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(Utf8Path::FromUtf8(directory), error))
        {
            if (error) break;
            if (!entry.is_directory()) continue;

            result.push_back(Utf8Path::ToUtf8(entry.path().filename()));
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        std::ostringstream output;
        output << input.rdbuf();
        return output.str();
    }

    std::string GetXmlTagValue(const std::string& text, const std::string& name)
    {
        std::string openTag = "<" + name + ">";
        std::string closeTag = "</" + name + ">";
        std::size_t begin = text.find(openTag);
        if (begin == std::string::npos) return std::string();

        begin += openTag.size();
        std::size_t end = text.find(closeTag, begin);
        return end == std::string::npos ? std::string() : text.substr(begin, end - begin);
    }

    std::string Quote(const std::string& value)
    {
        return "\"" + value + "\"";
    }

    std::string CMakeDefine(const char* name, const char* type, const std::string& value)
    {
        return Quote("-D" + std::string(name) + ":" + type + "=" + value);
    }

    std::string FindFirstCsproj(const std::filesystem::path& directory)
    {
        if (!std::filesystem::is_directory(directory)) return std::string();

        List<std::string> projects;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
        {
            if (error) break;
            if (entry.is_regular_file() && entry.path().extension() == ".csproj")
            {
                projects.push_back(ToCleanPath(entry.path()));
            }
        }

        std::sort(projects.begin(), projects.end());
        return projects.empty() ? std::string() : projects.front();
    }

    std::string GetParentDirectory(const std::string& path)
    {
        std::filesystem::path value = Utf8Path::FromUtf8(path);
        return ToCleanPath(value.has_parent_path() ? value.parent_path() : std::filesystem::current_path());
    }

    bool FileExists(const std::string& path)
    {
        return !path.empty() && std::filesystem::exists(Utf8Path::FromUtf8(path));
    }

    //判断脚本源是否比程序集更新。
    bool IsProjectScriptBuildOutdated(const std::string& scriptRoot, const std::string& assemblyPath)
    {
        if (scriptRoot.empty() || assemblyPath.empty()) return false;
        std::filesystem::path assembly = Utf8Path::FromUtf8(assemblyPath);
        if (!std::filesystem::exists(assembly)) return true;

        std::error_code error;
        std::filesystem::file_time_type assemblyTime = std::filesystem::last_write_time(assembly, error);
        if (error) return true;

        std::filesystem::path root = Utf8Path::FromUtf8(scriptRoot);
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root, error))
        {
            if (error) return true;
            if (!entry.is_regular_file()) continue;

            std::filesystem::path extension = entry.path().extension();
            if (extension != ".cs" && extension != ".csproj" && extension != ".props" && extension != ".targets") continue;

            std::filesystem::file_time_type sourceTime = std::filesystem::last_write_time(entry.path(), error);
            if (error) return true;
            if (sourceTime > assemblyTime) return true;
        }

        return false;
    }

    bool ScriptProjectUsesLocalRuntimeDll(const std::string& csproj)
    {
        std::string content = ReadTextFile(Utf8Path::FromUtf8(csproj));
        return content.find("Lib\\OrbedenCore.CSharp.dll") != std::string::npos
            || content.find("Lib/OrbedenCore.CSharp.dll") != std::string::npos;
    }

    bool RefreshLocalRuntimeDllReference(const std::string& csproj, const std::string& runtimeDllPath, std::string& outError)
    {
        outError.clear();
        if (!ScriptProjectUsesLocalRuntimeDll(csproj)) return true;

        if (runtimeDllPath.empty() || !std::filesystem::exists(Utf8Path::FromUtf8(runtimeDllPath)))
        {
            outError = "OrbedenCore.CSharp.dll was not found. Build OrbedenCore.vcxproj first.";
            return false;
        }

        std::filesystem::path target = Utf8Path::FromUtf8(csproj).parent_path() / "Lib/OrbedenCore.CSharp.dll";
        std::filesystem::create_directories(target.parent_path());

        std::error_code equivalentError;
        if (std::filesystem::exists(target) && std::filesystem::equivalent(Utf8Path::FromUtf8(runtimeDllPath), target, equivalentError))
        {
            return true;
        }

        std::error_code copyError;
        std::filesystem::copy_file(Utf8Path::FromUtf8(runtimeDllPath),
            target,
            std::filesystem::copy_options::overwrite_existing,
            copyError);
        if (!copyError) return true;

        outError = "Copy OrbedenCore.CSharp.dll failed: " + copyError.message();
        return false;
    }

    std::filesystem::path GetVisualStudioRoot()
    {
        return std::filesystem::path("C:/Program Files/Microsoft Visual Studio/18/Community");
    }

    std::string GetBundledCMakePath()
    {
        std::filesystem::path path = GetVisualStudioRoot() / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe";
        return std::filesystem::exists(path) ? ToCleanPath(path) : std::string();
    }

    std::string GetBundledNinjaPath()
    {
        std::filesystem::path path = GetVisualStudioRoot() / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe";
        return std::filesystem::exists(path) ? ToCleanPath(path) : std::string();
    }

    std::string GetBundledClangClPath()
    {
        const std::array<std::filesystem::path, 3> candidates =
        {
            GetVisualStudioRoot() / "VC/Tools/Llvm/bin/clang-cl.exe",
            GetVisualStudioRoot() / "VC/Tools/Llvm/x64/bin/clang-cl.exe",
            GetVisualStudioRoot() / "VC/Tools/Llvm/x86/bin/clang-cl.exe",
        };

        for (const std::filesystem::path& path : candidates)
        {
            if (std::filesystem::exists(path))
            {
                return ToCleanPath(path);
            }
        }

        return std::string();
    }

    std::string GetCMakeCommand()
    {
        std::string bundled = GetBundledCMakePath();
        return bundled.empty() ? "cmake" : Quote(bundled);
    }

    std::filesystem::path CopyAssemblyToShadowCache(const std::filesystem::path& source, const std::filesystem::path& shadowDirectory)
    {
        auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::filesystem::path targetDirectory = shadowDirectory / ("inspector_" + std::to_string(ticks));
        std::filesystem::create_directories(targetDirectory);
        return targetDirectory / source.filename();
    }

    bool CopyManagedFileIfExists(const std::filesystem::path& source, const std::filesystem::path& target)
    {
        if (!std::filesystem::exists(source)) return true;

        std::error_code error;
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
        return !error;
    }

    //复制目录下的托管依赖文件。
    bool CopyManagedDirectoryFiles(const std::filesystem::path& sourceDirectory, const std::filesystem::path& targetDirectory)
    {
        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(sourceDirectory, error))
        {
            if (error) return false;
            if (!entry.is_regular_file()) continue;

            std::filesystem::path extension = entry.path().extension();
            if (extension != ".dll" && extension != ".pdb" && extension != ".json") continue;

            std::filesystem::path target = targetDirectory / entry.path().filename();
            if (!CopyManagedFileIfExists(entry.path(), target)) return false;
        }

        return true;
    }

    //复制额外托管依赖目录。
    bool CopyManagedDependencyDirectories(const List<std::string>& directories, const std::filesystem::path& targetDirectory)
    {
        for (const std::string& directoryText : directories)
        {
            if (directoryText.empty()) continue;

            std::filesystem::path directory = Utf8Path::FromUtf8(directoryText);
            if (!std::filesystem::is_directory(directory)) continue;
            if (!CopyManagedDirectoryFiles(directory, targetDirectory)) return false;
        }

        return true;
    }

    std::string ShadowCopyManagedAssembly(const std::string& assemblyPath,
        const std::filesystem::path& shadowDirectory,
        const List<std::string>& managedDependencyDirectories)
    {
        std::filesystem::path sourceAssembly = std::filesystem::absolute(Utf8Path::FromUtf8(assemblyPath));
        if (!std::filesystem::exists(sourceAssembly)) return std::string();

        std::filesystem::path shadowAssembly = CopyAssemblyToShadowCache(sourceAssembly, shadowDirectory);
        std::filesystem::path sourcePdb = std::filesystem::path(sourceAssembly).replace_extension(".pdb");
        std::filesystem::path sourceDeps = std::filesystem::path(sourceAssembly).replace_extension(".deps.json");
        std::filesystem::path shadowPdb = std::filesystem::path(shadowAssembly).replace_extension(".pdb");
        std::filesystem::path shadowDeps = std::filesystem::path(shadowAssembly).replace_extension(".deps.json");
        if (!CopyManagedDirectoryFiles(sourceAssembly.parent_path(), shadowAssembly.parent_path())
            || !CopyManagedDependencyDirectories(managedDependencyDirectories, shadowAssembly.parent_path())
            || !CopyManagedFileIfExists(sourceAssembly, shadowAssembly)
            || !CopyManagedFileIfExists(sourcePdb, shadowPdb)
            || !CopyManagedFileIfExists(sourceDeps, shadowDeps))
        {
            return std::string();
        }

        return ToCleanPath(shadowAssembly);
    }

}

EditorSystem::EditorSystem(Application& application, const char* startupExecutablePath)
    : app(application)
    , project(application)
    , executablePath(startupExecutablePath ? startupExecutablePath : "")
    , editorScene(application, panelManager, managedBridge)
{
    previousInputEnabled = InputManager::IsEnabled();
    InputManager::SetEnabled(false);
    SetDialogDirectory(ToCleanPath(std::filesystem::current_path()));
    for (std::unique_ptr<IEditorPanel>& panel : EditorPanelRegistry::CreatePanels(*this))
    {
        panelManager.RegisterPanel(std::move(panel));
    }
    CopyToBuffer(newProjectNameBuffer, sizeof(newProjectNameBuffer), "NewGame");

    std::filesystem::path executableDirectory = GetExecutableDirectory(executablePath);
    std::filesystem::path managedDirectory = executableDirectory / "Managed";
    EditorClrHostConfig clrConfig;
    clrConfig.runtimeConfigPath = ToCleanPath(executableDirectory / "OrbedenEditor.runtimeconfig.json");
    clrConfig.componentAssemblyPath = ToCleanPath(managedDirectory / "Orbeden.Editor.dll");
    if (clrHost.Initialize(clrConfig))
    {
        managedBridge.Initialize(clrHost, *this, panelManager, editorScene.GetGizmoApi(), executablePath);
    }

    if (RenderSystem* renderSystem = app.GetSystem<RenderSystem>())
    {
        renderSystem->SetRenderOverlay(this);
        renderSystem->SetFpsLabelVisible(false);
    }
}

EditorSystem::~EditorSystem()
{
    SaveEditorLayout();
    app.SetPaused(false);
    app.SetSimulationEnabled(false);
    playMode.Stop();
    managedBridge.UnloadGameAssembly();
    managedBridge.Shutdown();
    clrHost.Shutdown();
    InputManager::SetEnabled(previousInputEnabled);

    if (RenderSystem* renderSystem = app.GetSystem<RenderSystem>())
    {
        renderSystem->SetRenderOverlay(nullptr);
    }
}

void EditorSystem::Update(World& world, float deltaTime)
{
    if (!playMode.IsPlaying())
    {
        editorScene.Update(world, deltaTime);
    }
}

//请求编辑器重绘并唤醒事件循环
void EditorSystem::RequestRepaint()
{
    if (repaintRequested.exchange(true, std::memory_order_acq_rel)) return;
    if (IWindow* window = app.GetWindow()) window->WakeEventLoop();
}

//获取并清除编辑器重绘请求
bool EditorSystem::TakeRepaintRequest()
{
    return repaintRequested.exchange(false, std::memory_order_acq_rel);
}

//判断编辑器是否需要连续重绘
bool EditorSystem::NeedsContinuousRepaint() const
{
    return continuousRepaint || (playMode.IsPlaying() && !app.IsPaused());
}

void EditorSystem::DrawOverlay()
{
    DrawMainMenuBar();
    DrawPlayToolbar();
    DrawProjectDialog();
    DrawNewProjectDialog();
    if (!playMode.IsPlaying()) editorScene.PruneSelection(app.GetWorld());
    panelManager.DrawPanels();

    if (!playMode.IsPlaying())
    {
        editorScene.DrawBackground();
    }
    else
    {
        editorScene.CancelInteraction();
    }

    if (playMode.IsPlaying())
    {
        app.GetSystem<ScriptSystem>()->DrawOverlay();
    }

    //活跃控件保持连续帧，释放鼠标后再补一帧提交本帧产生的修改
    continuousRepaint = ImGui::IsAnyItemActive();
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        || ImGui::IsMouseReleased(ImGuiMouseButton_Right)
        || ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
    {
        RequestRepaint();
    }
}

void EditorSystem::RequestOpenProjectDialog()
{
    OpenProjectDialog();
    RequestRepaint();
}

void EditorSystem::RequestNewProjectDialog()
{
    OpenNewProjectDialog();
    RequestRepaint();
}

void EditorSystem::RequestSaveCurrentWorld()
{
    SaveCurrentWorld();
}

void EditorSystem::RequestBuildScripts()
{
    if (playMode.IsPlaying())
    {
        RequestStop();
    }

    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        return;
    }

    std::string csproj = GetProjectScriptProjectPath();
    if (csproj.empty())
    {
        projectStatus = "No C# project found in script root.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string projectRepairError;
    if (!NewProjectGenerator::RepairScriptProjectBuildProps(csproj, projectRepairError))
    {
        projectStatus = projectRepairError;
        return;
    }

    std::string runtimeRefreshError;
    if (!RefreshLocalRuntimeDllReference(csproj, FindRuntimeCSharpDll(), runtimeRefreshError))
    {
        projectStatus = runtimeRefreshError;
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string command = "dotnet build " + Quote(csproj) + " -c Debug";
    if (RunCommand(command, "Build Game C#"))
    {
        RefreshInspectorGameAssembly();
        projectStatus = "Built Game C#: " + GetProjectGameAssemblyPath();
    }
}

void EditorSystem::RequestPlay()
{
    if (playMode.IsPlaying()) return;
    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        return;
    }

    std::string assemblyPath = GetProjectGameAssemblyPath();
    if (!FileExists(assemblyPath) || IsProjectScriptBuildOutdated(project.GetScriptRootPath(), assemblyPath))
    {
        RequestBuildScripts();
        assemblyPath = GetProjectGameAssemblyPath();
        if (IsProjectScriptBuildOutdated(project.GetScriptRootPath(), assemblyPath))
        {
            projectStatus = "C# build failed or output is out of date.";
            Log::Error(projectStatus.c_str());
            return;
        }
    }

    if (!FileExists(assemblyPath))
    {
        projectStatus = "Game DLL is missing. Build Game C# first: " + assemblyPath;
        Log::Error(projectStatus.c_str());
        return;
    }

    SaveEditorLayout();
    if (!SaveCurrentWorld())
    {
        return;
    }

    editorScene.EnterPlayMode(app.GetWorld());

    std::filesystem::path shadowDirectory = Utf8Path::FromUtf8(project.GetManagedRootPath()) / ".pie";
    std::string gameModuleType = GetProjectGameModuleTypeName();
    ScriptSystem* scriptSystem = app.GetSystem<ScriptSystem>();
    if (!scriptSystem
        || !playMode.Start(*scriptSystem, clrHost, assemblyPath, gameModuleType,
            ToCleanPath(shadowDirectory), GetManagedDependencyDirectories()))
    {
        projectStatus = playMode.GetLastError();
        editorScene.RestoreCamera(app.GetWorld());
        return;
    }

    managedBridge.LoadGameAssembly(playMode.GetShadowAssemblyPath(), GetProjectScriptSidecarPath());
    app.SetPaused(false);
    app.SetSimulationEnabled(true);
    projectStatus = "Play-In-Editor started.";
    RequestRepaint();
}

void EditorSystem::RequestStop()
{
    if (!playMode.IsPlaying()) return;

    app.SetPaused(false);
    app.SetSimulationEnabled(false);
    playMode.Stop();
    managedBridge.UnloadGameAssembly();
    if (project.HasProject())
    {
        if (project.ReloadStartupWorld())
        {
            editorScene.ExitPlayMode(app.GetWorld());
        }
        else
        {
            editorScene.ExitPlayMode(app.GetWorld());
            projectStatus = project.GetLastError();
            return;
        }
    }

    RefreshInspectorGameAssembly();
    projectStatus = "Play-In-Editor stopped.";
    RequestRepaint();
}

void EditorSystem::RequestBuildPlayer()
{
    if (playMode.IsPlaying())
    {
        RequestStop();
    }

    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string repoRoot = FindRepositoryRoot();
    if (repoRoot.empty())
    {
        projectStatus = "Repository root was not found.";
        Log::Error(projectStatus.c_str());
        return;
    }

    const PlayerTargetPlatformInfo& target = GetPlayerTargetPlatformInfo(selectedPlayerTargetPlatform);
    if (std::strcmp(target.scriptName, "Switch") == 0)
    {
        projectStatus = "Switch Player build requires vendor SDK/RID integration. No DLL fallback was attempted.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string scriptProject = GetProjectScriptProjectPath();
    if (scriptProject.empty())
    {
        projectStatus = "Build Player failed: no C# project found in script root.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string assemblyName = GetProjectGameAssemblyName();
    if (assemblyName.empty())
    {
        projectStatus = "Build Player failed: game assembly name was not found.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string publishError;
    if (!managedBridge.PublishGameAot(repoRoot,
        project.GetProjectRoot(),
        scriptProject,
        BuildConfiguration,
        target.scriptName,
        publishError))
    {
        projectStatus = "Build Player AOT failed for " + std::string(target.displayName) + ".";
        if (!publishError.empty()) projectStatus += " " + publishError;
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string aotLibraryName = GetNativeAotLibraryName(target, assemblyName);
    std::string aotLibraryPath = ToCleanPath(Utf8Path::FromUtf8(project.GetProjectRoot())
        / "Aot"
        / target.aotDirectory
        / BuildConfiguration
        / Utf8Path::FromUtf8(aotLibraryName));
    if (!FileExists(aotLibraryPath))
    {
        projectStatus = "Build Player failed: NativeAOT library was not found: " + aotLibraryPath;
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string cmakeCommand = GetCMakeCommand();
    std::string configureCommand = cmakeCommand + " --preset " + Quote(target.cmakePreset)
        + " " + CMakeDefine("CMAKE_BUILD_TYPE", "STRING", BuildConfiguration)
        + " " + CMakeDefine("ORBEDEN_GAME_AOT_LIB", "FILEPATH", aotLibraryPath)
        + " " + CMakeDefine("ORBEDEN_PROJECT_DIR", "PATH", project.GetProjectRoot());
    std::string ninjaPath = GetBundledNinjaPath();
    if (!ninjaPath.empty())
    {
        configureCommand += " " + CMakeDefine("CMAKE_MAKE_PROGRAM", "FILEPATH", ninjaPath);
    }
    if (std::strcmp(target.scriptName, "WindowsX64") == 0)
    {
        std::string clangClPath = GetBundledClangClPath();
        if (!clangClPath.empty())
        {
            configureCommand += " " + CMakeDefine("CMAKE_C_COMPILER", "FILEPATH", clangClPath);
            configureCommand += " " + CMakeDefine("CMAKE_CXX_COMPILER", "FILEPATH", clangClPath);
        }
    }
    if (!RunCommand(configureCommand, "Configure Player"))
    {
        projectStatus = "Configure Player failed for " + std::string(target.displayName) + ". Check CMake, Ninja, toolchain, and GLFW dependencies.";
        return;
    }

    std::string buildCommand = cmakeCommand + " --build --preset " + Quote(target.cmakePreset);
    if (!RunCommand(buildCommand, "Build Player"))
    {
        projectStatus = "Build Player failed for " + std::string(target.displayName) + ".";
        return;
    }

    projectStatus = "Built Player (" + std::string(target.displayName) + "): OrbedenGame/Build/" + target.cmakeBuildDirectory + "/bin/OrbedenGame";
}

bool EditorSystem::IsPlaying() const
{
    return playMode.IsPlaying();
}

bool EditorSystem::HasProject() const
{
    return project.HasProject();
}

const std::string& EditorSystem::GetProjectName() const
{
    return project.GetProjectName();
}

const std::string& EditorSystem::GetProjectRoot() const
{
    return project.GetProjectRoot();
}

std::string EditorSystem::GetProjectScriptRootPath() const
{
    return project.GetScriptRootPath();
}

std::string EditorSystem::GetProjectManagedRootPath() const
{
    return project.GetManagedRootPath();
}

std::string EditorSystem::GetStartupWorldPath() const
{
    return project.GetStartupWorldPath();
}

const std::string& EditorSystem::GetProjectStatusText() const
{
    return projectStatus;
}

int32 EditorSystem::GetPlayerTargetPlatformCount() const
{
    return static_cast<int32>(PlayerTargetPlatforms.size());
}

int32 EditorSystem::GetSelectedPlayerTargetPlatformIndex() const
{
    return selectedPlayerTargetPlatform;
}

const char* EditorSystem::GetPlayerTargetPlatformName(int32 index) const
{
    return GetPlayerTargetPlatformInfo(index).displayName;
}

void EditorSystem::SetSelectedPlayerTargetPlatformIndex(int32 index)
{
    if (index < 0 || index >= GetPlayerTargetPlatformCount()) return;

    selectedPlayerTargetPlatform = index;
}

const char* EditorSystem::GetSelectedPlayerTargetPlatformName() const
{
    return GetPlayerTargetPlatformInfo(selectedPlayerTargetPlatform).displayName;
}

World& EditorSystem::GetWorld()
{
    return app.GetWorld();
}

const World& EditorSystem::GetWorld() const
{
    return app.GetWorld();
}

EditorScene& EditorSystem::GetEditorScene()
{
    return editorScene;
}

const EditorScene& EditorSystem::GetEditorScene() const
{
    return editorScene;
}

//绘制一个托管面板
void EditorSystem::DrawManagedPanel(int32 handle)
{
    managedBridge.DrawPanel(handle, editorScene.GetSelectedEns(), editorScene.GetSelectedStableId());
}

//设置托管面板可见状态
void EditorSystem::SetManagedPanelVisible(int32 handle, bool visible)
{
    managedBridge.SetPanelVisible(handle, visible);
}

std::string EditorSystem::GetProjectScriptProjectPath() const
{
    return FindFirstCsproj(project.GetScriptRootPath());
}

std::string EditorSystem::GetProjectGameAssemblyName() const
{
    std::string csproj = GetProjectScriptProjectPath();
    if (csproj.empty()) return std::string();

    std::string content = ReadTextFile(Utf8Path::FromUtf8(csproj));
    std::string assemblyName = GetXmlTagValue(content, "AssemblyName");
    if (!assemblyName.empty()) return assemblyName;

    return Utf8Path::ToUtf8(Utf8Path::FromUtf8(csproj).stem());
}

std::string EditorSystem::GetProjectGameModuleTypeName() const
{
    std::string csproj = GetProjectScriptProjectPath();
    std::string assemblyName = GetProjectGameAssemblyName();
    if (csproj.empty() || assemblyName.empty()) return std::string();

    std::string content = ReadTextFile(Utf8Path::FromUtf8(csproj));
    std::string rootNamespace = GetXmlTagValue(content, "RootNamespace");
    if (rootNamespace.empty()) rootNamespace = assemblyName;

    return rootNamespace + ".GameModule, " + assemblyName;
}

std::string EditorSystem::GetProjectGameAssemblyPath() const
{
    std::string assemblyName = GetProjectGameAssemblyName();
    if (assemblyName.empty()) return std::string();

    return ToCleanPath(Utf8Path::FromUtf8(project.GetManagedRootPath()) / Utf8Path::FromUtf8(assemblyName + ".dll"));
}

std::string EditorSystem::GetProjectScriptSidecarPath() const
{
    std::string worldPath = project.GetStartupWorldPath();
    return worldPath.empty() ? std::string() : worldPath + ".scripts.json";
}

bool EditorSystem::RefreshInspectorGameAssembly()
{
    if (!project.HasProject())
    {
        managedBridge.UnloadGameAssembly();
        return false;
    }

    std::string assemblyPath = GetProjectGameAssemblyPath();
    std::string sidecarPath = GetProjectScriptSidecarPath();
    if (!FileExists(assemblyPath))
    {
        managedBridge.LoadGameAssembly(std::string(), sidecarPath);
        return false;
    }

    std::filesystem::path shadowDirectory = Utf8Path::FromUtf8(project.GetManagedRootPath()) / ".inspector";
    std::string shadowAssemblyPath = ShadowCopyManagedAssembly(assemblyPath, shadowDirectory, GetManagedDependencyDirectories());
    if (shadowAssemblyPath.empty())
    {
        managedBridge.LoadGameAssembly(std::string(), sidecarPath);
        projectStatus = "Inspector game assembly shadow copy failed.";
        Log::Warning(projectStatus.c_str());
        return false;
    }

    managedBridge.LoadGameAssembly(shadowAssemblyPath, sidecarPath);
    return true;
}

std::string EditorSystem::FindRepositoryRoot() const
{
    List<std::filesystem::path> starts;
    starts.push_back(GetExecutableDirectory(executablePath));
    starts.push_back(std::filesystem::current_path());
    if (project.HasProject())
    {
        starts.push_back(Utf8Path::FromUtf8(project.GetProjectRoot()));
    }

    for (std::filesystem::path start : starts)
    {
        start = std::filesystem::absolute(start);
        while (!start.empty())
        {
            if (std::filesystem::exists(start / "orbeden.slnx"))
            {
                return ToCleanPath(start);
            }

            std::filesystem::path parent = start.parent_path();
            if (parent == start) break;
            start = parent;
        }
    }

    return std::string();
}

std::string EditorSystem::FindRuntimeCSharpDll() const
{
    constexpr const char* RuntimeDllRelativePath = "Sdk/Managed/OrbedenCore.CSharp/OrbedenCore.CSharp.dll";

    List<std::filesystem::path> candidates;
    std::filesystem::path executableDirectory = GetExecutableDirectory(executablePath);
    candidates.push_back(executableDirectory / RuntimeDllRelativePath);

    std::filesystem::path parentDirectory = executableDirectory.parent_path();
    if (!parentDirectory.empty())
    {
        candidates.push_back(parentDirectory / RuntimeDllRelativePath);
        std::filesystem::path editorDirectory = parentDirectory.parent_path();
        if (!editorDirectory.empty())
        {
            candidates.push_back(editorDirectory / RuntimeDllRelativePath);
        }
    }

    std::string repoRoot = FindRepositoryRoot();
    if (!repoRoot.empty())
    {
        candidates.push_back(Utf8Path::FromUtf8(repoRoot) / "OrbedenEditor" / RuntimeDllRelativePath);
        candidates.push_back(Utf8Path::FromUtf8(repoRoot) / "OrbedenGame" / RuntimeDllRelativePath);
    }

    for (const std::filesystem::path& path : candidates)
    {
        if (std::filesystem::exists(path))
        {
            return ToCleanPath(path);
        }
    }

    return std::string();
}

//Debug模式下同步Core C#运行库到当前游戏项目
bool EditorSystem::SyncProjectRuntimeCSharpDll(std::string& outError) const
{
    outError.clear();
#if defined(NDEBUG)
    return true;
#else
    if (!project.HasProject()) return true;

    std::string runtimeDll = FindRuntimeCSharpDll();
    if (runtimeDll.empty())
    {
        outError = "OrbedenCore.CSharp.dll was not found. Build OrbedenCore.vcxproj first.";
        return false;
    }

    std::string scriptRoot = project.GetScriptRootPath();
    if (scriptRoot.empty())
    {
        outError = "Project script root is empty.";
        return false;
    }

    std::filesystem::path source = Utf8Path::FromUtf8(runtimeDll);
    std::filesystem::path target = Utf8Path::FromUtf8(scriptRoot) / "Lib/OrbedenCore.CSharp.dll";
    std::error_code error;
    if (std::filesystem::exists(target)
        && std::filesystem::equivalent(source, target, error)
        && !error)
    {
        return true;
    }

    error.clear();
    std::filesystem::create_directories(target.parent_path(), error);
    if (error)
    {
        outError = "Create project script Lib directory failed: " + error.message();
        return false;
    }

    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
    if (error)
    {
        outError = "Copy OrbedenCore.CSharp.dll failed: " + error.message();
        return false;
    }

    Log::Info(("Synchronized Core C# runtime: " + ToCleanPath(target)).c_str());
    return true;
#endif
}

//获取 Play/Inspector 需要复制的托管依赖目录
List<std::string> EditorSystem::GetManagedDependencyDirectories() const
{
    List<std::string> directories;
    std::string runtimeDll = FindRuntimeCSharpDll();
    if (!runtimeDll.empty())
    {
        directories.push_back(ToCleanPath(Utf8Path::FromUtf8(runtimeDll).parent_path()));
    }

    return directories;
}

bool EditorSystem::RunCommand(const std::string& command, const char* actionName)
{
#if defined(_WIN32)
    std::wstring nativeCommand = Utf8Path::FromUtf8(command).wstring();
    int result = _wsystem(nativeCommand.c_str());
#else
    int result = std::system(command.c_str());
#endif
    if (result == 0)
    {
        projectStatus = std::string(actionName) + " succeeded.";
        return true;
    }

    projectStatus = std::string(actionName) + " failed.";
    Log::Error(projectStatus.c_str());
    return false;
}

bool EditorSystem::SaveCurrentWorld()
{
    if (playMode.IsPlaying())
    {
        projectStatus = "Stop Play-In-Editor before saving.";
        Log::Warning(projectStatus.c_str());
        return false;
    }

    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        Log::Warning(projectStatus.c_str());
        return false;
    }

    World& world = app.GetWorld();
    bool hadEditorCamera = editorScene.RemoveCameraForSerialization(world);

    bool saved = project.SaveStartupWorld();
    projectStatus = saved ? ("Saved: " + project.GetStartupWorldPath()) : project.GetLastError();

    if (hadEditorCamera) editorScene.RestoreCamera(world);
    editorScene.CancelInteraction();
    return saved;
}

//保存当前编辑器布局
void EditorSystem::SaveEditorLayout()
{
    if (!project.HasProject()) return;

    EditorLayoutState layout;
    panelManager.WriteLayout(layout);
    editorScene.WriteLayout(layout);
    project.SaveEditorLayout(layout);
}

//应用当前项目编辑器布局
void EditorSystem::ApplyEditorLayout()
{
    if (!project.HasProject()) return;

    const EditorLayoutState& layout = project.GetEditorLayout();
    panelManager.ApplyLayout(layout);
    editorScene.ApplyLayout(layout, app.GetWorld());
}

void EditorSystem::OpenProjectDialog()
{
    openProjectDialog = true;
    dialogError.clear();
    if (project.HasProject())
    {
        SetDialogDirectory(project.GetProjectRoot());
    }
}

void EditorSystem::OpenNewProjectDialog()
{
    newProjectDialog = true;
    dialogError.clear();
    if (project.HasProject())
    {
        SetDialogDirectory(GetParentDirectory(project.GetProjectRoot()));
    }
    else
    {
        SetDialogDirectory(ToCleanPath(std::filesystem::current_path()));
    }

    if (newProjectNameBuffer[0] == '\0')
    {
        CopyToBuffer(newProjectNameBuffer, sizeof(newProjectNameBuffer), "NewGame");
    }
}

void EditorSystem::DrawMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("Project"))
    {
        if (project.HasProject())
        {
            ImGui::TextUnformatted(project.GetProjectName().c_str());
            ImGui::Separator();
        }

        if (!project.HasProject() || playMode.IsPlaying())
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Selectable("Save", false))
        {
            SaveCurrentWorld();
        }

        if (!project.HasProject() || playMode.IsPlaying())
        {
            ImGui::EndDisabled();
        }

        if (ImGui::Selectable("New...", false))
        {
            OpenNewProjectDialog();
        }

        if (ImGui::Selectable("Load...", false))
        {
            OpenProjectDialog();
        }

        if (!projectStatus.empty())
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", projectStatus.c_str());
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Views"))
    {
        panelManager.DrawViewsMenu();
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

//绘制顶部播放工具栏
void EditorSystem::DrawPlayToolbar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return;

    constexpr float32 toolbarHeight = 34.0f;
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove;

    bool open = ImGui::BeginViewportSideBar("##EditorPlayToolbar", viewport, ImGuiDir_Up, toolbarHeight, flags);
    if (open)
    {
        float32 buttonWidth = 36.0f;
        float32 spacing = ImGui::GetStyle().ItemSpacing.x;
        float32 totalWidth = buttonWidth * 3.0f + spacing * 2.0f;
        ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowWidth() - totalWidth) * 0.5f));
        ImGui::SetCursorPosY(5.0f);

        //播放按钮。
        bool playDisabled = playMode.IsPlaying();
        if (playDisabled)
        {
            ImGui::BeginDisabled();
        }
        if (DrawToolbarIconButton("##play_button", ToolbarIcon::Play, ImVec2(buttonWidth, 24.0f)))
        {
            RequestPlay();
        }
        if (playDisabled)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        //暂停按钮。
        bool pauseDisabled = !playMode.IsPlaying();
        bool pauseHighlighted = app.IsPaused();
        if (pauseDisabled)
        {
            ImGui::BeginDisabled();
        }
        if (pauseHighlighted)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (DrawToolbarIconButton("##pause_button", ToolbarIcon::Pause, ImVec2(buttonWidth, 24.0f)))
        {
            app.SetPaused(!app.IsPaused());
        }
        if (pauseHighlighted)
        {
            ImGui::PopStyleColor();
        }
        if (pauseDisabled)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        //停止按钮。
        bool stopDisabled = !playMode.IsPlaying();
        if (stopDisabled)
        {
            ImGui::BeginDisabled();
        }
        if (DrawToolbarIconButton("##stop_button", ToolbarIcon::Stop, ImVec2(buttonWidth, 24.0f)))
        {
            RequestStop();
        }
        if (stopDisabled)
        {
            ImGui::EndDisabled();
        }
    }

    ImGui::End();
}

void EditorSystem::DrawProjectDialog()
{
    if (openProjectDialog)
    {
        ImGui::OpenPopup("Load Project Folder");
        openProjectDialog = false;
    }

    ImGui::SetNextWindowSize(ImVec2(640.0f, 460.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Load Project Folder", nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    ImGui::InputText("Path", pathBuffer, sizeof(pathBuffer));
    dialogDirectory = pathBuffer;

    if (ImGui::Button("Up"))
    {
        std::filesystem::path parent = Utf8Path::FromUtf8(dialogDirectory).parent_path();
        if (!parent.empty())
        {
            SetDialogDirectory(ToCleanPath(parent));
        }
    }

    ImGui::Separator();

    ImGui::BeginChild("ProjectDirectoryList", ImVec2(0.0f, 300.0f), true);
    if (std::filesystem::is_directory(dialogDirectory))
    {
        for (const std::string& child : GetChildDirectories(dialogDirectory))
        {
            if (ImGui::Selectable(child.c_str()))
            {
                SetDialogDirectory(ToCleanPath(Utf8Path::FromUtf8(dialogDirectory) / Utf8Path::FromUtf8(child)));
            }
        }
    }
    else
    {
        ImGui::TextUnformatted("Directory does not exist.");
    }
    ImGui::EndChild();

    if (!dialogError.empty())
    {
        ImGui::TextWrapped("%s", dialogError.c_str());
    }

    if (ImGui::Button("Load"))
    {
        RequestStop();
        SaveEditorLayout();
        if (project.LoadProjectFolder(dialogDirectory))
        {
            editorScene.ClearSceneState();
            dialogError.clear();
            SetDialogDirectory(project.GetProjectRoot());
            projectStatus = "Loaded: " + project.GetProjectRoot();
            std::string runtimeSyncError;
            if (!SyncProjectRuntimeCSharpDll(runtimeSyncError))
            {
                projectStatus += " Core C# sync failed: " + runtimeSyncError;
                Log::Warning(runtimeSyncError.c_str());
            }
            ApplyEditorLayout();
            RefreshInspectorGameAssembly();
            ImGui::CloseCurrentPopup();
        }
        else
        {
            dialogError = project.GetLastError();
            projectStatus = dialogError;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        dialogError.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorSystem::DrawNewProjectDialog()
{
    if (newProjectDialog)
    {
        ImGui::OpenPopup("New Project");
        newProjectDialog = false;
    }

    ImGui::SetNextWindowSize(ImVec2(640.0f, 500.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    ImGui::InputText("Parent Path", pathBuffer, sizeof(pathBuffer));
    dialogDirectory = pathBuffer;
    ImGui::InputText("Project Name", newProjectNameBuffer, sizeof(newProjectNameBuffer));

    if (ImGui::Button("Up"))
    {
        std::filesystem::path parent = Utf8Path::FromUtf8(dialogDirectory).parent_path();
        if (!parent.empty())
        {
            SetDialogDirectory(ToCleanPath(parent));
        }
    }

    ImGui::Separator();

    ImGui::BeginChild("NewProjectDirectoryList", ImVec2(0.0f, 300.0f), true);
    if (std::filesystem::is_directory(dialogDirectory))
    {
        for (const std::string& child : GetChildDirectories(dialogDirectory))
        {
            if (ImGui::Selectable(child.c_str()))
            {
                SetDialogDirectory(ToCleanPath(Utf8Path::FromUtf8(dialogDirectory) / Utf8Path::FromUtf8(child)));
            }
        }
    }
    else
    {
        ImGui::TextUnformatted("Parent directory does not exist.");
    }
    ImGui::EndChild();

    if (!dialogError.empty())
    {
        ImGui::TextWrapped("%s", dialogError.c_str());
    }

    if (ImGui::Button("Create"))
    {
        std::string runtimeDllPath = FindRuntimeCSharpDll();
        if (runtimeDllPath.empty())
        {
            dialogError = "OrbedenCore.CSharp.dll was not found. Build OrbedenCore.vcxproj first.";
            projectStatus = dialogError;
        }
        else
        {
            RequestStop();
            SaveEditorLayout();

            std::string projectRoot;
            std::string error;
            if (NewProjectGenerator::CreateProject(dialogDirectory, newProjectNameBuffer, runtimeDllPath, projectRoot, error)
                && project.LoadProjectFolder(projectRoot))
            {
                editorScene.ClearSceneState();
                dialogError.clear();
                SetDialogDirectory(project.GetProjectRoot());
                projectStatus = "Created project: " + project.GetProjectRoot();
                std::string runtimeSyncError;
                if (!SyncProjectRuntimeCSharpDll(runtimeSyncError))
                {
                    projectStatus += " Core C# sync failed: " + runtimeSyncError;
                    Log::Warning(runtimeSyncError.c_str());
                }
                ApplyEditorLayout();
                RefreshInspectorGameAssembly();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                dialogError = !error.empty() ? error : project.GetLastError();
                projectStatus = dialogError;
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        dialogError.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorSystem::SetDialogDirectory(const std::string& path)
{
    dialogDirectory = ToCleanPath(Utf8Path::FromUtf8(path));
    CopyToBuffer(pathBuffer, sizeof(pathBuffer), dialogDirectory);
}
