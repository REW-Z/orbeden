#include "Runtime/Managed/ScriptSystem.h"

#include "Log/Log.h"

#define NETHOST_USE_AS_STATIC
#include <nethost.h>
#include <coreclr_delegates.h>
#include <hostfxr.h>

#include <filesystem>
#include <sstream>
#include <vector>

#if defined(_WIN32)
extern "C"
{
    __declspec(dllimport) void* __stdcall LoadLibraryW(const wchar_t* fileName);
    __declspec(dllimport) int __stdcall FreeLibrary(void* module);
    __declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
    __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int codePage,
        unsigned long flags,
        const wchar_t* wideText,
        int wideTextLength,
        char* text,
        int textLength,
        const char* defaultChar,
        int* usedDefaultChar);
}
#else
#include <dlfcn.h>
#endif

namespace
{
#if defined(_WIN32)
    // Windows 下的 UTF-8 代码页编号，避免包含完整 Win32 头。
    constexpr unsigned int WindowsCodePageUtf8 = 65001;
#endif

    using HostString = std::basic_string<char_t>;

#if defined(_WIN32)
    static_assert(sizeof(char_t) == sizeof(wchar_t), "char_t must match wchar_t on Windows.");
#endif

    hostfxr_initialize_for_runtime_config_fn InitializeForRuntimeConfig = nullptr;
    hostfxr_get_runtime_delegate_fn GetRuntimeDelegate = nullptr;
    hostfxr_close_fn CloseHostfxr = nullptr;
    hostfxr_set_error_writer_fn SetErrorWriter = nullptr;

    HostString ToHostString(const std::string& value)
    {
#if defined(_WIN32)
        std::wstring wide = std::filesystem::path(value).wstring();
        return HostString(wide.begin(), wide.end());
#else
        return value;
#endif
    }

    std::string FromHostString(const char_t* value)
    {
        if (value == nullptr) return std::string();

#if defined(_WIN32)
        const wchar_t* wideValue = reinterpret_cast<const wchar_t*>(value);
        int length = WideCharToMultiByte(WindowsCodePageUtf8, 0, wideValue, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0) return std::string();

        std::string result(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(WindowsCodePageUtf8, 0, wideValue, -1, result.data(), length, nullptr, nullptr);
        if (!result.empty() && result.back() == '\0')
        {
            result.pop_back();
        }

        return result;
#else
        return std::string(value);
#endif
    }

    std::string NormalizePath(const std::string& path)
    {
        if (path.empty()) return std::string();
        return std::filesystem::absolute(std::filesystem::path(path)).lexically_normal().generic_string();
    }

    void WriteHostfxrError(const char_t* message)
    {
        std::string text = FromHostString(message);
        if (!text.empty())
        {
            Log::Error(text.c_str());
        }
    }

    std::string FormatHostError(const char* action, int32_t result)
    {
        std::ostringstream stream;
        stream << action << " failed. hostfxr result: 0x" << std::hex << result;
        return stream.str();
    }

    bool IsHostSuccess(int32_t result)
    {
        return result >= 0;
    }

    void* LoadNativeLibrary(const HostString& path)
    {
#if defined(_WIN32)
        return LoadLibraryW(reinterpret_cast<const wchar_t*>(path.c_str()));
#else
        return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
    }

    void CloseNativeLibrary(void* library)
    {
        if (library == nullptr) return;

#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
    }

    void* LoadNativeSymbol(void* library, const char* name)
    {
        if (library == nullptr || name == nullptr) return nullptr;

#if defined(_WIN32)
        return GetProcAddress(library, name);
#else
        return dlsym(library, name);
#endif
    }

    bool LoadHostfxrFunctions(void* library)
    {
        // 解析 hostfxr 的核心入口函数。
        InitializeForRuntimeConfig = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
            LoadNativeSymbol(library, "hostfxr_initialize_for_runtime_config"));
        GetRuntimeDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
            LoadNativeSymbol(library, "hostfxr_get_runtime_delegate"));
        CloseHostfxr = reinterpret_cast<hostfxr_close_fn>(
            LoadNativeSymbol(library, "hostfxr_close"));
        SetErrorWriter = reinterpret_cast<hostfxr_set_error_writer_fn>(
            LoadNativeSymbol(library, "hostfxr_set_error_writer"));

        return InitializeForRuntimeConfig != nullptr
            && GetRuntimeDelegate != nullptr
            && CloseHostfxr != nullptr
            && SetErrorWriter != nullptr;
    }

    bool ResolveHostfxrPath(const ScriptSystemConfig& config, HostString& resolvedPath, std::string& error)
    {
        // 让 nethost 根据配置查找当前平台可用的 hostfxr。
        HostString dotnetRoot = ToHostString(config.dotnetRoot);
        HostString componentAssemblyPath = ToHostString(config.componentAssemblyPath);

        get_hostfxr_parameters parameters{};
        parameters.size = sizeof(get_hostfxr_parameters);
        parameters.dotnet_root = dotnetRoot.empty() ? nullptr : dotnetRoot.c_str();
        parameters.assembly_path = componentAssemblyPath.empty() ? nullptr : componentAssemblyPath.c_str();

        std::vector<char_t> buffer(4096);
        size_t bufferSize = buffer.size();
        int32_t result = get_hostfxr_path(buffer.data(), &bufferSize, &parameters);
        if (result != 0 && bufferSize > buffer.size())
        {
            buffer.resize(bufferSize);
            result = get_hostfxr_path(buffer.data(), &bufferSize, &parameters);
        }

        if (result != 0)
        {
            error = FormatHostError("get_hostfxr_path", result);
            return false;
        }

        resolvedPath.assign(buffer.data());
        return true;
    }

    const char_t* OptionalHostString(const HostString& value)
    {
        return value.empty() ? nullptr : value.c_str();
    }
}

ScriptSystem::~ScriptSystem()
{
    Shutdown();
}

bool ScriptSystem::Initialize(const ScriptSystemConfig& config)
{
    if (IsInitialized()) return true;

    lastError.clear();
    hostfxrPath.clear();

    // 检查 runtimeconfig.json 是否存在。
    std::string runtimeConfigPath = NormalizePath(config.runtimeConfigPath);
    if (runtimeConfigPath.empty())
    {
        lastError = "ScriptSystem requires a runtimeconfig.json path.";
        Log::Error(lastError.c_str());
        return false;
    }

    if (!std::filesystem::exists(runtimeConfigPath))
    {
        lastError = "ScriptSystem runtimeconfig.json does not exist: " + runtimeConfigPath;
        Log::Error(lastError.c_str());
        return false;
    }

    // 查找并加载 hostfxr 动态库。
    HostString resolvedHostfxrPath;
    if (!ResolveHostfxrPath(config, resolvedHostfxrPath, lastError))
    {
        Log::Error(lastError.c_str());
        return false;
    }

    hostfxrPath = FromHostString(resolvedHostfxrPath.c_str());
    hostfxrLibrary = LoadNativeLibrary(resolvedHostfxrPath);
    if (hostfxrLibrary == nullptr)
    {
        lastError = "Failed to load hostfxr: " + hostfxrPath;
        Log::Error(lastError.c_str());
        return false;
    }

    // 从 hostfxr 中解析初始化和委托获取函数。
    if (!LoadHostfxrFunctions(hostfxrLibrary))
    {
        lastError = "Failed to load required hostfxr exports.";
        Log::Error(lastError.c_str());
        Shutdown();
        return false;
    }

    SetErrorWriter(&WriteHostfxrError);

    // 使用 runtimeconfig 初始化托管运行时上下文。
    HostString runtimeConfigHostPath = ToHostString(runtimeConfigPath);
    HostString dotnetRoot = ToHostString(config.dotnetRoot);
    HostString componentAssemblyPath = ToHostString(config.componentAssemblyPath);

    hostfxr_initialize_parameters parameters{};
    parameters.size = sizeof(hostfxr_initialize_parameters);
    parameters.dotnet_root = OptionalHostString(dotnetRoot);
    parameters.host_path = OptionalHostString(componentAssemblyPath);

    hostfxr_handle context = nullptr;
    int32_t result = InitializeForRuntimeConfig(runtimeConfigHostPath.c_str(), &parameters, &context);
    if (!IsHostSuccess(result) || context == nullptr)
    {
        lastError = FormatHostError("hostfxr_initialize_for_runtime_config", result);
        Log::Error(lastError.c_str());
        Shutdown();
        return false;
    }

    // 获取后续加载 C# 入口函数所需的运行时委托。
    void* runtimeDelegate = nullptr;
    result = GetRuntimeDelegate(context, hdt_load_assembly_and_get_function_pointer, &runtimeDelegate);
    if (!IsHostSuccess(result) || runtimeDelegate == nullptr)
    {
        lastError = FormatHostError("hostfxr_get_runtime_delegate", result);
        Log::Error(lastError.c_str());
        CloseHostfxr(context);
        Shutdown();
        return false;
    }

    hostContext = context;
    loadAssemblyAndGetFunctionPointer = runtimeDelegate;

    Log::Info("ScriptSystem initialized.");
    return true;
}

void ScriptSystem::Shutdown()
{
    // 关闭 hostfxr 上下文。
    if (hostContext != nullptr && CloseHostfxr != nullptr)
    {
        CloseHostfxr(static_cast<hostfxr_handle>(hostContext));
    }

    hostContext = nullptr;
    loadAssemblyAndGetFunctionPointer = nullptr;

    if (SetErrorWriter != nullptr)
    {
        SetErrorWriter(nullptr);
    }

    // 卸载 hostfxr 动态库并清空函数指针。
    CloseNativeLibrary(hostfxrLibrary);
    hostfxrLibrary = nullptr;

    InitializeForRuntimeConfig = nullptr;
    GetRuntimeDelegate = nullptr;
    CloseHostfxr = nullptr;
    SetErrorWriter = nullptr;
}

bool ScriptSystem::IsInitialized() const
{
    return hostContext != nullptr && loadAssemblyAndGetFunctionPointer != nullptr;
}

const std::string& ScriptSystem::GetHostfxrPath() const
{
    return hostfxrPath;
}

const std::string& ScriptSystem::GetLastError() const
{
    return lastError;
}

bool ScriptSystem::LoadAssemblyFunction(const std::string& assemblyPath,
    const std::string& typeName,
    const std::string& methodName,
    const std::string& delegateTypeName,
    void** functionPointer)
{
    // 校验输出参数和运行时状态。
    if (functionPointer == nullptr)
    {
        lastError = "ScriptSystem function pointer output is null.";
        Log::Error(lastError.c_str());
        return false;
    }

    *functionPointer = nullptr;

    if (!IsInitialized())
    {
        lastError = "ScriptSystem is not initialized.";
        Log::Error(lastError.c_str());
        return false;
    }

    // 检查托管程序集文件。
    std::string normalizedAssemblyPath = NormalizePath(assemblyPath);
    if (!std::filesystem::exists(normalizedAssemblyPath))
    {
        lastError = "Managed assembly does not exist: " + normalizedAssemblyPath;
        Log::Error(lastError.c_str());
        return false;
    }

    HostString assemblyHostPath = ToHostString(normalizedAssemblyPath);
    HostString typeHostName = ToHostString(typeName);
    HostString methodHostName = ToHostString(methodName);
    HostString delegateHostTypeName = ToHostString(delegateTypeName);

    load_assembly_and_get_function_pointer_fn loadFunction =
        reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAssemblyAndGetFunctionPointer);

    // 让 .NET 加载程序集并返回指定静态方法的函数指针。
    int32_t result = loadFunction(
        assemblyHostPath.c_str(),
        typeHostName.c_str(),
        methodHostName.c_str(),
        OptionalHostString(delegateHostTypeName),
        nullptr,
        functionPointer);

    if (!IsHostSuccess(result) || *functionPointer == nullptr)
    {
        lastError = FormatHostError("load_assembly_and_get_function_pointer", result);
        Log::Error(lastError.c_str());
        return false;
    }

    return true;
}
