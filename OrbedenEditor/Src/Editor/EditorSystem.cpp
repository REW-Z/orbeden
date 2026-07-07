#include "Editor/EditorSystem.h"

#include "Log/Log.h"
#include "Editor/NewProjectGenerator.h"
#include "Platform/GlfwWindow.h"
#include "Platform/InputManager.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Ens.h"
#include "FileSystem/PathDefines.h"
#include "Runtime/Object/SpaceComponent.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <imgui.h>
#include <sstream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace
{
    constexpr float32 Pi = 3.14159265358979323846f;
    constexpr const char* EditorCameraId = "world://editor/camera";

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
        return path.lexically_normal().generic_string();
    }

    std::filesystem::path GetExecutableDirectory(const std::string& executablePath)
    {
        if (executablePath.empty()) return std::filesystem::current_path();

        std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(executablePath));
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
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
        {
            if (error) break;
            if (!entry.is_directory()) continue;

            result.push_back(entry.path().filename().string());
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    quaternion Mul(const quaternion& a, const quaternion& b)
    {
        return
        {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        };
    }

    quaternion GetYawPitchRotation(float32 yawDegrees, float32 pitchDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        float32 pitch = pitchDegrees * Pi / 180.0f;
        quaternion yawRotation = { 0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f) };
        quaternion pitchRotation = { std::sin(pitch * 0.5f), 0.0f, 0.0f, std::cos(pitch * 0.5f) };
        return Mul(yawRotation, pitchRotation);
    }

    vector3 ForwardFromYawPitch(float32 yawDegrees, float32 pitchDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        float32 pitch = pitchDegrees * Pi / 180.0f;
        float32 cosPitch = std::cos(pitch);
        return RenderMath::Normalize({ -std::sin(yaw) * cosPitch, std::sin(pitch), -std::cos(yaw) * cosPitch });
    }

    vector3 RightFromYaw(float32 yawDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        return RenderMath::Normalize({ std::cos(yaw), 0.0f, -std::sin(yaw) });
    }

    vector3 Add(const vector3& a, const vector3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    vector3 Scale(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    vector3 UpFromYawPitch(float32 yawDegrees, float32 pitchDegrees)
    {
        vector3 right = RightFromYaw(yawDegrees);
        vector3 forward = ForwardFromYawPitch(yawDegrees, pitchDegrees);
        return RenderMath::Normalize(RenderMath::Cross(right, forward));
    }

    GLFWwindow* GetGlfwWindow(Application& app)
    {
        GlfwWindow* glfwWindow = dynamic_cast<GlfwWindow*>(app.GetWindow());
        return glfwWindow ? glfwWindow->GetGlfwWindow() : nullptr;
    }

    bool IsMouseDown(GLFWwindow* window, int button)
    {
        return window && glfwGetMouseButton(window, button) == GLFW_PRESS;
    }

    bool ImGuiCapturesMouse()
    {
        return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
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
        std::filesystem::path value(path);
        return ToCleanPath(value.has_parent_path() ? value.parent_path() : std::filesystem::current_path());
    }

    bool FileExists(const std::string& path)
    {
        return !path.empty() && std::filesystem::exists(std::filesystem::path(path));
    }

    bool ScriptProjectUsesLocalRuntimeDll(const std::string& csproj)
    {
        std::string content = ReadTextFile(std::filesystem::path(csproj));
        return content.find("Lib\\OrbedenCore.CSharp.dll") != std::string::npos
            || content.find("Lib/OrbedenCore.CSharp.dll") != std::string::npos;
    }

    bool RefreshLocalRuntimeDllReference(const std::string& csproj, const std::string& runtimeDllPath, std::string& outError)
    {
        outError.clear();
        if (!ScriptProjectUsesLocalRuntimeDll(csproj)) return true;

        if (runtimeDllPath.empty() || !std::filesystem::exists(std::filesystem::path(runtimeDllPath)))
        {
            outError = "OrbedenCore.CSharp.dll was not found. Build OrbedenCore.CSharp first.";
            return false;
        }

        std::filesystem::path target = std::filesystem::path(csproj).parent_path() / "Lib/OrbedenCore.CSharp.dll";
        std::filesystem::create_directories(target.parent_path());

        std::error_code equivalentError;
        if (std::filesystem::exists(target) && std::filesystem::equivalent(std::filesystem::path(runtimeDllPath), target, equivalentError))
        {
            return true;
        }

        std::error_code copyError;
        std::filesystem::copy_file(std::filesystem::path(runtimeDllPath),
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
        std::filesystem::create_directories(shadowDirectory);

        auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::string fileName = source.stem().string() + "_inspector_" + std::to_string(ticks) + source.extension().string();
        return shadowDirectory / fileName;
    }

    bool CopyManagedFileIfExists(const std::filesystem::path& source, const std::filesystem::path& target)
    {
        if (!std::filesystem::exists(source)) return true;

        std::error_code error;
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
        return !error;
    }

    std::string ShadowCopyManagedAssembly(const std::string& assemblyPath, const std::filesystem::path& shadowDirectory)
    {
        std::filesystem::path sourceAssembly = std::filesystem::absolute(std::filesystem::path(assemblyPath));
        if (!std::filesystem::exists(sourceAssembly)) return std::string();

        std::filesystem::path shadowAssembly = CopyAssemblyToShadowCache(sourceAssembly, shadowDirectory);
        std::filesystem::path sourcePdb = std::filesystem::path(sourceAssembly).replace_extension(".pdb");
        std::filesystem::path sourceDeps = std::filesystem::path(sourceAssembly).replace_extension(".deps.json");
        std::filesystem::path shadowPdb = std::filesystem::path(shadowAssembly).replace_extension(".pdb");
        std::filesystem::path shadowDeps = std::filesystem::path(shadowAssembly).replace_extension(".deps.json");
        if (!CopyManagedFileIfExists(sourceAssembly, shadowAssembly)
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
    , projectPanel(*this)
    , ensViewPanel(*this)
{
    previousInputEnabled = InputManager::IsEnabled();
    InputManager::SetEnabled(false);
    SetDialogDirectory(ToCleanPath(std::filesystem::current_path()));
    RegisterBuiltInPanels();
    CopyToBuffer(newProjectNameBuffer, sizeof(newProjectNameBuffer), "NewGame");

    std::filesystem::path executableDirectory = GetExecutableDirectory(executablePath);
    std::filesystem::path managedDirectory = executableDirectory / "Managed";
    EditorClrHostConfig clrConfig;
    clrConfig.runtimeConfigPath = ToCleanPath(executableDirectory / "OrbedenEditor.runtimeconfig.json");
    clrConfig.componentAssemblyPath = ToCleanPath(managedDirectory / "Orbeden.Editor.dll");
    if (clrHost.Initialize(clrConfig))
    {
        managedOverlay.Initialize(clrHost, executablePath);
    }
}

EditorSystem::~EditorSystem()
{
    playMode.Stop();
    managedOverlay.UnloadGameAssembly();
    managedOverlay.Shutdown();
    clrHost.Shutdown();
    InputManager::SetEnabled(previousInputEnabled);

    if (RenderSystem* renderSystem = app.GetRenderSystem())
    {
        renderSystem->SetRenderOverlay(nullptr);
    }
}

void EditorSystem::Update(World& world, float deltaTime)
{
    TryAutoLoadExampleProject();
    playMode.Update(deltaTime);
    UpdateEditorCamera(world, deltaTime);
}

void EditorSystem::Render(World& world, float deltaTime)
{
    (void)world;
    (void)deltaTime;

    if (RenderSystem* renderSystem = app.GetRenderSystem())
    {
        renderSystem->SetRenderOverlay(this);
        renderSystem->SetFpsLabelVisible(false);
    }
}

void EditorSystem::DrawOverlay()
{
    DrawMainMenuBar();
    DrawManagedSceneGizmos();
    DrawProjectDialog();
    DrawNewProjectDialog();
    panelManager.DrawPanels();
    managedOverlay.DrawInspector(selection.GetSelectedEns(), GetSelectedEnsStableId());
    managedOverlay.DrawPanels();
    playMode.DrawGui();
}

void EditorSystem::RequestOpenProjectDialog()
{
    OpenProjectDialog();
}

void EditorSystem::RequestNewProjectDialog()
{
    OpenNewProjectDialog();
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

    std::string repoRoot = FindRepositoryRoot();
    if (!repoRoot.empty())
    {
        std::filesystem::path coreCSharpProject = std::filesystem::path(repoRoot) / "OrbedenCore/Managed/OrbedenCore.CSharp/OrbedenCore.CSharp.csproj";
        if (std::filesystem::exists(coreCSharpProject))
        {
            std::string coreCommand = "dotnet build " + Quote(ToCleanPath(coreCSharpProject)) + " -c Debug";
            if (!RunCommand(coreCommand, "Build Core C#"))
            {
                return;
            }
        }
    }

    std::string runtimeRefreshError;
    if (!RefreshLocalRuntimeDllReference(csproj, FindRuntimeCSharpDll(), runtimeRefreshError))
    {
        projectStatus = runtimeRefreshError;
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string command = "dotnet build " + Quote(csproj) + " -c Debug";
    if (RunCommand(command, "Build C#"))
    {
        RefreshInspectorGameAssembly();
        projectStatus = "Built C#: " + GetProjectGameAssemblyPath();
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
    if (!FileExists(assemblyPath))
    {
        projectStatus = "Game DLL is missing. Build C# first: " + assemblyPath;
        Log::Error(projectStatus.c_str());
        return;
    }

    std::filesystem::path shadowDirectory = std::filesystem::path(project.GetManagedRootPath()) / ".pie";
    std::string gameModuleType = GetProjectGameModuleTypeName();
    if (!playMode.Start(clrHost, assemblyPath, gameModuleType, ToCleanPath(shadowDirectory)))
    {
        projectStatus = playMode.GetLastError();
        return;
    }

    managedOverlay.LoadGameAssembly(playMode.GetShadowAssemblyPath(), GetProjectScriptSidecarPath());
    projectStatus = "Play-In-Editor started.";
}

void EditorSystem::RequestStop()
{
    playMode.Stop();
    managedOverlay.UnloadGameAssembly();
    RefreshInspectorGameAssembly();
    projectStatus = "Play-In-Editor stopped.";
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

    std::string publishScript = ToCleanPath(std::filesystem::path(repoRoot) / "Build/PublishGameAot.ps1");
    if (!FileExists(publishScript))
    {
        projectStatus = "NativeAOT publish script is missing.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string publishCommand = "powershell -ExecutionPolicy Bypass -File " + Quote(publishScript)
        + " -Configuration " + BuildConfiguration
        + " -TargetPlatform " + target.scriptName
        + " -ProjectRoot " + Quote(project.GetProjectRoot())
        + " -ScriptProject " + Quote(scriptProject);
    if (!RunCommand(publishCommand, "Build Player AOT"))
    {
        projectStatus = "Build Player AOT failed for " + std::string(target.displayName) + ". Check local NativeAOT packs; no DLL fallback was attempted.";
        return;
    }

    std::string aotLibraryName = GetNativeAotLibraryName(target, assemblyName);
    std::string aotLibraryPath = ToCleanPath(std::filesystem::path(project.GetProjectRoot()) / "Aot" / target.aotDirectory / BuildConfiguration / aotLibraryName);
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

EditorSelection& EditorSystem::GetSelection()
{
    return selection;
}

const EditorSelection& EditorSystem::GetSelection() const
{
    return selection;
}

std::string EditorSystem::GetProjectScriptProjectPath() const
{
    return FindFirstCsproj(project.GetScriptRootPath());
}

std::string EditorSystem::GetProjectGameAssemblyName() const
{
    std::string csproj = GetProjectScriptProjectPath();
    if (csproj.empty()) return std::string();

    std::string content = ReadTextFile(std::filesystem::path(csproj));
    std::string assemblyName = GetXmlTagValue(content, "AssemblyName");
    if (!assemblyName.empty()) return assemblyName;

    return std::filesystem::path(csproj).stem().string();
}

std::string EditorSystem::GetProjectGameModuleTypeName() const
{
    std::string csproj = GetProjectScriptProjectPath();
    std::string assemblyName = GetProjectGameAssemblyName();
    if (csproj.empty() || assemblyName.empty()) return std::string();

    std::string content = ReadTextFile(std::filesystem::path(csproj));
    std::string rootNamespace = GetXmlTagValue(content, "RootNamespace");
    if (rootNamespace.empty()) rootNamespace = assemblyName;

    return rootNamespace + ".GameModule, " + assemblyName;
}

std::string EditorSystem::GetProjectGameAssemblyPath() const
{
    std::string assemblyName = GetProjectGameAssemblyName();
    if (assemblyName.empty()) return std::string();

    return ToCleanPath(std::filesystem::path(project.GetManagedRootPath()) / (assemblyName + ".dll"));
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
        managedOverlay.UnloadGameAssembly();
        return false;
    }

    std::string assemblyPath = GetProjectGameAssemblyPath();
    std::string sidecarPath = GetProjectScriptSidecarPath();
    if (!FileExists(assemblyPath))
    {
        managedOverlay.LoadGameAssembly(std::string(), sidecarPath);
        return false;
    }

    std::filesystem::path shadowDirectory = std::filesystem::path(project.GetManagedRootPath()) / ".inspector";
    std::string shadowAssemblyPath = ShadowCopyManagedAssembly(assemblyPath, shadowDirectory);
    if (shadowAssemblyPath.empty())
    {
        managedOverlay.LoadGameAssembly(std::string(), sidecarPath);
        projectStatus = "Inspector game assembly shadow copy failed.";
        Log::Warning(projectStatus.c_str());
        return false;
    }

    managedOverlay.LoadGameAssembly(shadowAssemblyPath, sidecarPath);
    return true;
}

std::string EditorSystem::GetSelectedEnsStableId() const
{
    EnsId selectedEns = selection.GetSelectedEns();
    if (selectedEns.IsNull()) return std::string();

    SpaceComponent* space = app.GetWorld().GetSpaceComponent(selectedEns);
    return space ? space->GetInstanceId().GetPath() : std::string();
}

std::string EditorSystem::FindRepositoryRoot() const
{
    List<std::filesystem::path> starts;
    starts.push_back(GetExecutableDirectory(executablePath));
    starts.push_back(std::filesystem::current_path());
    if (project.HasProject())
    {
        starts.push_back(std::filesystem::path(project.GetProjectRoot()));
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
        candidates.push_back(std::filesystem::path(repoRoot) / "OrbedenEditor" / RuntimeDllRelativePath);
        candidates.push_back(std::filesystem::path(repoRoot) / "OrbedenGame" / RuntimeDllRelativePath);
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

bool EditorSystem::RunCommand(const std::string& command, const char* actionName)
{
    int result = std::system(command.c_str());
    if (result == 0)
    {
        projectStatus = std::string(actionName) + " succeeded.";
        return true;
    }

    projectStatus = std::string(actionName) + " failed.";
    Log::Error(projectStatus.c_str());
    return false;
}

void EditorSystem::RegisterBuiltInPanels()
{
    PanelInfo projectInfo;
    projectInfo.id = projectPanel.GetPanelId();
    projectInfo.title = projectPanel.GetPanelTitle();
    projectInfo.defaultVisible = true;
    projectInfo.defaultSize = { 360.0f, 180.0f };
    panelManager.RegisterPanel(projectInfo, &projectPanel);

    PanelInfo ensViewInfo;
    ensViewInfo.id = ensViewPanel.GetPanelId();
    ensViewInfo.title = ensViewPanel.GetPanelTitle();
    ensViewInfo.defaultVisible = true;
    ensViewInfo.defaultSize = { 320.0f, 420.0f };
    panelManager.RegisterPanel(ensViewInfo, &ensViewPanel);
}

void EditorSystem::CreateEditorCameraIfMissing(World& world)
{
    Ens* cameraEns = world.GetEns(editorCameraEns);
    if (!cameraEns)
    {
        cameraEns = world.FindEns(StringId(EditorCameraId));
    }
    if (!cameraEns)
    {
        cameraEns = world.CreateEnsWithStableId(EditorCameraId, "EditorCamera");
        if (cameraEns)
        {
            if (SpaceComponent* space = cameraEns->Space())
            {
                space->localPosition = { 5.0f, 3.2f, 7.0f };
                space->localScale = { 1.0f, 1.0f, 1.0f };
            }
        }
    }
    if (!cameraEns) return;

    Camera* camera = cameraEns->GetComponent<Camera>();
    if (!camera)
    {
        camera = cameraEns->AddComponent<Camera>();
    }
    if (camera)
    {
        camera->enabled = true;
        camera->fieldOfView = 60.0f;
        camera->nearPlane = 0.1f;
        camera->farPlane = 1000.0f;
        camera->depth = 10000.0f;
        camera->clearMode = ClearMode::SolidColor;
        camera->clearColor = { 0.62f, 0.78f, 0.96f, 1.0f };
    }

    if (SpaceComponent* space = cameraEns->Space())
    {
        space->localRotation = GetYawPitchRotation(cameraYaw, cameraPitch);
    }
    editorCameraEns = cameraEns->GetId();
}

void EditorSystem::UpdateEditorCamera(World& world, float deltaTime)
{
    (void)deltaTime;
    CreateEditorCameraIfMissing(world);

    SpaceComponent* space = world.GetSpaceComponent(editorCameraEns);
    GLFWwindow* window = GetGlfwWindow(app);
    if (!space || !window) return;

    if (!ImGuiCapturesMouse())
    {
        int32 mode = 0;
        if (IsMouseDown(window, GLFW_MOUSE_BUTTON_MIDDLE))
        {
            mode = 2;
        }
        else if (IsMouseDown(window, GLFW_MOUSE_BUTTON_RIGHT))
        {
            mode = 3;
        }
        else if (IsMouseDown(window, GLFW_MOUSE_BUTTON_LEFT))
        {
            mode = 1;
        }

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        if (mode != 0)
        {
            if (cameraMouseDragging && cameraMouseMode == mode)
            {
                float32 deltaX = static_cast<float32>(mouseX - previousMouseX);
                float32 deltaY = static_cast<float32>(mouseY - previousMouseY);
                if (mode == 1)
                {
                    cameraYaw -= deltaX * 0.12f;
                    cameraPitch -= deltaY * 0.12f;
                    cameraPitch = std::clamp(cameraPitch, -82.0f, 82.0f);
                }
                else if (mode == 2)
                {
                    constexpr float32 panScale = 0.01f;
                    vector3 right = RightFromYaw(cameraYaw);
                    vector3 up = UpFromYawPitch(cameraYaw, cameraPitch);
                    vector3 pan = Add(Scale(right, -deltaX * panScale), Scale(up, deltaY * panScale));
                    space->localPosition = Add(space->localPosition, pan);
                }
                else if (mode == 3)
                {
                    float32 dollyScale = cameraMoveSpeed * 0.01f;
                    vector3 forward = ForwardFromYawPitch(cameraYaw, cameraPitch);
                    space->localPosition = Add(space->localPosition, Scale(forward, -deltaY * dollyScale));
                }
            }

            cameraMouseDragging = true;
            cameraMouseMode = mode;
            previousMouseX = mouseX;
            previousMouseY = mouseY;
        }
        else
        {
            cameraMouseDragging = false;
            cameraMouseMode = 0;
        }
    }
    else
    {
        cameraMouseDragging = false;
        cameraMouseMode = 0;
    }

    space->localRotation = GetYawPitchRotation(cameraYaw, cameraPitch);
}

void EditorSystem::SaveCurrentWorld()
{
    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        Log::Warning(projectStatus.c_str());
        return;
    }

    World& world = app.GetWorld();
    Ens* cameraEns = world.GetEns(editorCameraEns);
    if (!cameraEns)
    {
        cameraEns = world.FindEns(StringId(EditorCameraId));
    }

    bool hadEditorCamera = cameraEns != nullptr;
    vector3 editorCameraPosition;
    quaternion editorCameraRotation;
    if (hadEditorCamera)
    {
        if (SpaceComponent* space = cameraEns->Space())
        {
            editorCameraPosition = space->localPosition;
            editorCameraRotation = space->localRotation;
        }

        cameraEns->Destroy();
        editorCameraEns = EnsId();
    }

    bool saved = project.SaveStartupWorld();
    projectStatus = saved ? ("Saved: " + project.GetStartupWorldPath()) : project.GetLastError();

    CreateEditorCameraIfMissing(world);
    if (hadEditorCamera)
    {
        if (SpaceComponent* space = world.GetSpaceComponent(editorCameraEns))
        {
            space->localPosition = editorCameraPosition;
            space->localRotation = editorCameraRotation;
        }
    }

    cameraMouseDragging = false;
    cameraMouseMode = 0;
}

void EditorSystem::TryAutoLoadExampleProject()
{
    if (autoLoadAttempted) return;
    autoLoadAttempted = true;

#if !defined(NDEBUG)
    std::string exampleProject = PathDefines::FindProjectRoot("ExampleProject", executablePath);
    if (exampleProject.empty())
    {
        Log::Warning("Editor Debug auto-load skipped: ExampleProject was not found.");
        return;
    }

    RequestStop();
    project.LoadProjectFolder(exampleProject);
    if (project.HasProject())
    {
        SetDialogDirectory(project.GetProjectRoot());
        projectStatus = "Loaded: " + project.GetProjectRoot();
        selection.Clear();
        CreateEditorCameraIfMissing(app.GetWorld());
        RefreshInspectorGameAssembly();
    }
#endif
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

        if (!project.HasProject())
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Selectable("Save", false))
        {
            SaveCurrentWorld();
        }

        if (!project.HasProject())
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

void EditorSystem::DrawManagedSceneGizmos()
{
    World& world = app.GetWorld();
    SpaceComponent* space = world.GetSpaceComponent(editorCameraEns);
    Camera* camera = nullptr;
    if (Ens* cameraEns = world.GetEns(editorCameraEns))
    {
        camera = cameraEns->GetComponent<Camera>();
    }
    if (!space || !camera || !camera->enabled) return;

    // 使用当前 EditorCamera 计算 SceneView 的 VP 矩阵。
    IWindow* window = app.GetWindow();
    int32 width = window ? window->GetFramebufferWidth() : 0;
    int32 height = window ? window->GetFramebufferHeight() : 0;
    float32 aspect = height > 0 ? static_cast<float32>(width) / static_cast<float32>(height) : 1.0f;
    matrix4x4 worldMatrix = RenderMath::TRS(space->localPosition, space->localRotation, space->localScale);
    matrix4x4 viewMatrix = RenderMath::Inverse(worldMatrix);
    matrix4x4 projectionMatrix = RenderMath::Perspective(camera->fieldOfView, aspect, camera->nearPlane, camera->farPlane);
    matrix4x4 viewProjection = RenderMath::Mul(projectionMatrix, viewMatrix);

    managedOverlay.DrawSceneGizmos(viewProjection, width, height);
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
        std::filesystem::path parent = std::filesystem::path(dialogDirectory).parent_path();
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
                SetDialogDirectory(ToCleanPath(std::filesystem::path(dialogDirectory) / child));
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
        if (project.LoadProjectFolder(dialogDirectory))
        {
            dialogError.clear();
            SetDialogDirectory(project.GetProjectRoot());
            projectStatus = "Loaded: " + project.GetProjectRoot();
            selection.Clear();
            CreateEditorCameraIfMissing(app.GetWorld());
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
        std::filesystem::path parent = std::filesystem::path(dialogDirectory).parent_path();
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
                SetDialogDirectory(ToCleanPath(std::filesystem::path(dialogDirectory) / child));
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
            dialogError = "OrbedenCore.CSharp.dll was not found. Build OrbedenCore.CSharp first.";
            projectStatus = dialogError;
        }
        else
        {
            RequestStop();

            std::string projectRoot;
            std::string error;
            if (NewProjectGenerator::CreateProject(dialogDirectory, newProjectNameBuffer, runtimeDllPath, projectRoot, error)
                && project.LoadProjectFolder(projectRoot))
            {
                dialogError.clear();
                SetDialogDirectory(project.GetProjectRoot());
                projectStatus = "Created project: " + project.GetProjectRoot();
                selection.Clear();
                CreateEditorCameraIfMissing(app.GetWorld());
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
    dialogDirectory = ToCleanPath(std::filesystem::path(path));
    CopyToBuffer(pathBuffer, sizeof(pathBuffer), dialogDirectory);
}
