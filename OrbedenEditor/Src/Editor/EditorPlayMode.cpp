#include "Editor/EditorPlayMode.h"

#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"
#include <chrono>
#include <filesystem>
#include <sstream>

namespace
{
    constexpr const char* InitializeMethod = "OrbedenGame_Initialize";
    constexpr const char* ShutdownMethod = "OrbedenGame_Shutdown";
    constexpr const char* LoadAssemblyMethod = "OrbedenGame_LoadAssembly";
    constexpr const char* UpdateMethod = "OrbedenGame_Update";
    constexpr const char* FixedUpdateMethod = "OrbedenGame_FixedUpdate";
    constexpr const char* LateUpdateMethod = "OrbedenGame_LateUpdate";
    constexpr const char* EnsWorldActiveChangedMethod = "OrbedenGame_EnsWorldActiveChanged";
    constexpr const char* EnsDestroyedMethod = "OrbedenGame_EnsDestroyed";
    constexpr const char* DrawGuiMethod = "OrbedenGame_DrawGui";
    constexpr const char* GameModuleType = "Orbeden.GameModule, OrbedenCore.CSharp";

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

bool EditorPlayMode::Start(ScriptSystem& scripts,
    EditorClrHost& host,
    const std::string& gameAssemblyPath,
    const std::string& runtimeAssemblyPath,
    const std::string& shadowDirectory,
    const List<std::string>& managedDependencyDirectories)
{
    if (IsPlaying()) return true;

    ClearBindings();
    lastError.clear();
    if (!host.IsInitialized())
    {
        lastError = "Editor CLR host is not initialized.";
        Log::Error(lastError.c_str());
        return false;
    }

    std::filesystem::path sourceAssembly = std::filesystem::absolute(Utf8Path::FromUtf8(gameAssemblyPath));
    if (!std::filesystem::exists(sourceAssembly))
    {
        lastError = "Game assembly does not exist: " + ToCleanPath(sourceAssembly);
        Log::Error(lastError.c_str());
        return false;
    }

    std::filesystem::path runtimeAssembly = std::filesystem::absolute(Utf8Path::FromUtf8(runtimeAssemblyPath));
    if (!std::filesystem::exists(runtimeAssembly))
    {
        lastError = "Script runtime assembly does not exist: " + ToCleanPath(runtimeAssembly);
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
    ScriptEntryPoints entryPoints;
    if (!host.BindFunction(ToCleanPath(runtimeAssembly), GameModuleType, LoadAssemblyMethod, reinterpret_cast<void**>(&entryPoints.loadAssembly))
        || !host.BindFunction(ToCleanPath(runtimeAssembly), GameModuleType, InitializeMethod, reinterpret_cast<void**>(&entryPoints.initialize))
        || !host.BindFunction(ToCleanPath(runtimeAssembly), GameModuleType, ShutdownMethod, reinterpret_cast<void**>(&entryPoints.shutdown))
        || !host.BindFunction(ToCleanPath(runtimeAssembly), GameModuleType, UpdateMethod, reinterpret_cast<void**>(&entryPoints.update))
        || !host.BindFunction(ToCleanPath(runtimeAssembly), GameModuleType, FixedUpdateMethod, reinterpret_cast<void**>(&entryPoints.fixedUpdate))
        || !host.BindFunction(ToCleanPath(runtimeAssembly), GameModuleType, LateUpdateMethod, reinterpret_cast<void**>(&entryPoints.lateUpdate))
        || !host.BindFunction(ToCleanPath(runtimeAssembly), GameModuleType, EnsWorldActiveChangedMethod, reinterpret_cast<void**>(&entryPoints.ensWorldActiveChanged))
        || !host.BindFunction(ToCleanPath(runtimeAssembly), GameModuleType, EnsDestroyedMethod, reinterpret_cast<void**>(&entryPoints.ensDestroyed))
        || !host.BindFunction(ToCleanPath(runtimeAssembly), GameModuleType, DrawGuiMethod, reinterpret_cast<void**>(&entryPoints.drawGui)))
    {
        lastError = host.GetLastError() + " Assembly: " + ToCleanPath(runtimeAssembly) + " Type: " + GameModuleType;
        ClearBindings();
        return false;
    }

    if (!entryPoints.loadAssembly(reinterpret_cast<const uint8*>(shadowAssemblyPath.data()), static_cast<int32>(shadowAssemblyPath.size())))
    {
        lastError = "Script runtime could not load the game assembly: " + shadowAssemblyPath;
        ClearBindings();
        return false;
    }

    if (!scripts.SetClrEntryPoints(entryPoints) || !scripts.Initialize())
    {
        lastError = "ScriptSystem failed to initialize CLR game entry points.";
        ClearBindings();
        return false;
    }

    scriptSystem = &scripts;
    Log::Info("Editor Play-In-Editor started.");
    return true;
}

void EditorPlayMode::Stop()
{
    if (IsPlaying())
    {
        scriptSystem->Shutdown();
        Log::Info("Editor Play-In-Editor stopped.");
    }

    ClearBindings();
}

bool EditorPlayMode::IsPlaying() const
{
    return scriptSystem && scriptSystem->IsInitialized();
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
    scriptSystem = nullptr;
    shadowAssemblyPath.clear();
}
