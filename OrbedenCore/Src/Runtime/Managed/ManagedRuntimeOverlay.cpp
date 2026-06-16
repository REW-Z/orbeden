#include "Runtime/Managed/ManagedRuntimeOverlay.h"

#include "Log/Log.h"

#include <coreclr_delegates.h>
#include <filesystem>

namespace
{
    using ManagedDrawGuiFn = void(CORECLR_DELEGATE_CALLTYPE*)();

    // 检查文件是否存在，失败时写入日志。
    bool RequireFile(const std::string& path, const char* label)
    {
        if (std::filesystem::exists(path)) return true;

        std::string message = std::string(label) + " does not exist: " + path;
        Log::Warning(message.c_str());
        return false;
    }
}

bool ManagedRuntimeOverlay::Initialize(ScriptSystem& runtime, const ManagedRuntimeOverlayConfig& config)
{
    if (initialized) return true;
    if (!runtime.IsInitialized())
    {
        Log::Warning("ManagedRuntimeOverlay initialize skipped: ScriptSystem is not initialized.");
        return false;
    }

    scriptSystem = &runtime;

    // 可选绑定项目侧 OnGui。
    if (!config.userAssemblyPath.empty())
    {
        if (!RequireFile(config.userAssemblyPath, "Managed user assembly"))
        {
            scriptSystem = nullptr;
            return false;
        }

        if (!scriptSystem->BindCSharpFunction(config.userAssemblyPath,
            config.userTypeName,
            config.userMethodName,
            &DrawGuiFunction))
        {
            Log::Warning("ManagedRuntimeOverlay initialize skipped: user OnGui binding failed.");
            scriptSystem = nullptr;
            return false;
        }
    }

    initialized = true;
    return true;
}

void ManagedRuntimeOverlay::Shutdown()
{
    DrawGuiFunction = nullptr;
    initialized = false;
    scriptSystem = nullptr;
}

void ManagedRuntimeOverlay::DrawOverlay()
{
    if (!initialized || DrawGuiFunction == nullptr) return;

    // 调用项目 C# OnGui。
    ManagedDrawGuiFn DrawGui = reinterpret_cast<ManagedDrawGuiFn>(DrawGuiFunction);
    DrawGui();
}

bool ManagedRuntimeOverlay::IsInitialized() const
{
    return initialized;
}

ScriptSystem& ManagedRuntimeOverlay::GetScriptSystem()
{
    return *scriptSystem;
}
