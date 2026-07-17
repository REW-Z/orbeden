#include "Editor/EditorPlayMode.h"

#include "FileSystem/Utf8Path.h"
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
    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return Utf8Path::ToUtf8(path.lexically_normal());
    }

    //创建 shadow copy 目标路径。
    std::filesystem::path CopyAssemblyToShadowCache(const std::filesystem::path& source, const std::filesystem::path& shadowDirectory)
    {
        auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::filesystem::path targetDirectory = shadowDirectory / ("run_" + std::to_string(ticks));
        std::filesystem::create_directories(targetDirectory);
        return targetDirectory / source.filename();
    }

    //复制相关托管文件。
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
}

bool EditorPlayMode::Start(EditorClrHost& host,
    const std::string& assemblyPath,
    const std::string& gameModuleType,
    const std::string& shadowDirectory,
    const List<std::string>& managedDependencyDirectories)
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

    std::filesystem::path sourceAssembly = std::filesystem::absolute(Utf8Path::FromUtf8(assemblyPath));
    if (!std::filesystem::exists(sourceAssembly))
    {
        lastError = "Game assembly does not exist: " + ToCleanPath(sourceAssembly);
        Log::Error(lastError.c_str());
        return false;
    }

    std::filesystem::path shadowAssembly = CopyAssemblyToShadowCache(sourceAssembly, Utf8Path::FromUtf8(shadowDirectory));
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
        lastError = "Game assembly shadow copy failed.";
        Log::Error(lastError.c_str());
        return false;
    }

    shadowAssemblyPath = ToCleanPath(shadowAssembly);
    if (!host.BindFunction(shadowAssemblyPath, gameModuleType, InitializeMethod, reinterpret_cast<void**>(&InitializeGame))
        || !host.BindFunction(shadowAssemblyPath, gameModuleType, ShutdownMethod, reinterpret_cast<void**>(&ShutdownGame))
        || !host.BindFunction(shadowAssemblyPath, gameModuleType, UpdateMethod, reinterpret_cast<void**>(&UpdateGame))
        || !host.BindFunction(shadowAssemblyPath, gameModuleType, DrawGuiMethod, reinterpret_cast<void**>(&DrawGameGui)))
    {
        lastError = host.GetLastError() + " Assembly: " + shadowAssemblyPath + " Type: " + gameModuleType;
        ClearBindings();
        return false;
    }

    OrbedenNativeApi nativeApi = OrbedenNativeApi::Create();
    InitializeGame(&nativeApi);
    playing = true;
    paused = false;
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
    if (!playing || paused || !UpdateGame) return;
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

//设置播放暂停状态。
void EditorPlayMode::SetPaused(bool value)
{
    if (!playing)
    {
        paused = false;
        return;
    }

    paused = value;
}

//判断播放是否暂停。
bool EditorPlayMode::IsPaused() const
{
    return playing && paused;
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
    paused = false;
    shadowAssemblyPath.clear();
}
