#include "Editor/EditorPlayMode.h"

#include "Log/Log.h"
#include "Runtime/Native/OrbedenNativeApi.h"

#include <chrono>
#include <filesystem>
#include <sstream>

namespace
{
    constexpr const char* InitializeMethod = "OrbedenGame_Initialize";
    constexpr const char* ShutdownMethod = "OrbedenGame_Shutdown";
    constexpr const char* UpdateMethod = "OrbedenGame_Update";
    constexpr const char* DrawGuiMethod = "OrbedenGame_DrawGui";

    //规范化路径字符串。
    std::string NormalizePath(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    //创建 shadow copy 目标路径。
    std::filesystem::path MakeShadowAssemblyPath(const std::filesystem::path& source, const std::filesystem::path& shadowDirectory)
    {
        std::filesystem::create_directories(shadowDirectory);

        auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::string fileName = source.stem().string() + "_" + std::to_string(ticks) + source.extension().string();
        return shadowDirectory / fileName;
    }

    //复制相关托管文件。
    bool CopyManagedFileIfExists(const std::filesystem::path& source, const std::filesystem::path& target)
    {
        if (!std::filesystem::exists(source)) return true;

        std::error_code error;
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
        return !error;
    }
}

bool EditorPlayMode::Start(EditorClrHost& host,
    const std::string& assemblyPath,
    const std::string& gameModuleType,
    const std::string& shadowDirectory)
{
    if (playing) return true;

    ClearBindings();
    lastError.clear();
    if (!host.IsInitialized())
    {
        lastError = "Editor CLR host is not initialized.";
        Log::Error(lastError.c_str());
        return false;
    }

    std::filesystem::path sourceAssembly = std::filesystem::absolute(std::filesystem::path(assemblyPath));
    if (!std::filesystem::exists(sourceAssembly))
    {
        lastError = "Game assembly does not exist: " + NormalizePath(sourceAssembly);
        Log::Error(lastError.c_str());
        return false;
    }

    std::filesystem::path shadowAssembly = MakeShadowAssemblyPath(sourceAssembly, std::filesystem::path(shadowDirectory));
    std::filesystem::path sourcePdb = std::filesystem::path(sourceAssembly).replace_extension(".pdb");
    std::filesystem::path sourceDeps = std::filesystem::path(sourceAssembly).replace_extension(".deps.json");
    std::filesystem::path shadowPdb = std::filesystem::path(shadowAssembly).replace_extension(".pdb");
    std::filesystem::path shadowDeps = std::filesystem::path(shadowAssembly).replace_extension(".deps.json");
    if (!CopyManagedFileIfExists(sourceAssembly, shadowAssembly)
        || !CopyManagedFileIfExists(sourcePdb, shadowPdb)
        || !CopyManagedFileIfExists(sourceDeps, shadowDeps))
    {
        lastError = "Game assembly shadow copy failed.";
        Log::Error(lastError.c_str());
        return false;
    }

    shadowAssemblyPath = NormalizePath(shadowAssembly);
    if (!host.BindFunction(shadowAssemblyPath, gameModuleType, InitializeMethod, reinterpret_cast<void**>(&InitializeGame))
        || !host.BindFunction(shadowAssemblyPath, gameModuleType, ShutdownMethod, reinterpret_cast<void**>(&ShutdownGame))
        || !host.BindFunction(shadowAssemblyPath, gameModuleType, UpdateMethod, reinterpret_cast<void**>(&UpdateGame))
        || !host.BindFunction(shadowAssemblyPath, gameModuleType, DrawGuiMethod, reinterpret_cast<void**>(&DrawGameGui)))
    {
        lastError = host.GetLastError();
        ClearBindings();
        return false;
    }

    OrbedenNativeApi nativeApi = OrbedenNativeApi::Create();
    InitializeGame(&nativeApi);
    playing = true;
    Log::Info("Editor Play-In-Editor started.");
    return true;
}

void EditorPlayMode::Stop()
{
    if (playing && ShutdownGame)
    {
        ShutdownGame();
        Log::Info("Editor Play-In-Editor stopped.");
    }

    ClearBindings();
}

void EditorPlayMode::Update(float deltaTime)
{
    if (!playing || !UpdateGame) return;
    UpdateGame(deltaTime);
}

void EditorPlayMode::DrawGui()
{
    if (!playing || !DrawGameGui) return;
    DrawGameGui();
}

bool EditorPlayMode::IsPlaying() const
{
    return playing;
}

const std::string& EditorPlayMode::GetShadowAssemblyPath() const
{
    return shadowAssemblyPath;
}

const std::string& EditorPlayMode::GetLastError() const
{
    return lastError;
}

void EditorPlayMode::ClearBindings()
{
    InitializeGame = nullptr;
    ShutdownGame = nullptr;
    UpdateGame = nullptr;
    DrawGameGui = nullptr;
    playing = false;
    shadowAssemblyPath.clear();
}
