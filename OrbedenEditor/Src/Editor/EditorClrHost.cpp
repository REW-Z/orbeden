#include "Editor/EditorClrHost.h"

#include "Log/Log.h"
#include "Platform/DynamicLibrary.h"

#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>

#include <filesystem>
#include <sstream>
#include <vector>

using string_t = std::basic_string<char_t>;

namespace
{
    hostfxr_initialize_for_runtime_config_fn FuncInitForConfig = nullptr;
    hostfxr_get_runtime_delegate_fn FuncGetDelegate = nullptr;
    hostfxr_close_fn FuncClose = nullptr;
    hostfxr_set_error_writer_fn FuncSetErrorWriter = nullptr;

    //把普通路径字符串转换为 hostfxr 字符类型。
    string_t ToStringT(const std::string& value)
    {
#if defined(_WIN32)
        std::wstring wideValue = std::filesystem::path(value).wstring();
        return string_t(wideValue.begin(), wideValue.end());
#else
        return value;
#endif
    }

    //把 hostfxr 字符串转换为 UTF-8。
    std::string FromStringT(const char_t* value)
    {
        if (value == nullptr) return std::string();

#if defined(_WIN32)
        return std::filesystem::path(reinterpret_cast<const wchar_t*>(value)).generic_string();
#else
        return std::string(value);
#endif
    }

    //把 hostfxr 字符串转换为路径。
    std::filesystem::path ToPathT(const char_t* value)
    {
        if (value == nullptr) return std::filesystem::path();

#if defined(_WIN32)
        return std::filesystem::path(reinterpret_cast<const wchar_t*>(value));
#else
        return std::filesystem::path(value);
#endif
    }

    //规范化磁盘路径。
    std::string ToCleanPath(const std::string& path)
    {
        if (path.empty()) return std::string();
        return std::filesystem::absolute(std::filesystem::path(path)).lexically_normal().generic_string();
    }

    //输出 hostfxr 内部错误。
    void WriteHostfxrError(const char_t* message)
    {
        std::string text = FromStringT(message);
        if (!text.empty())
        {
            Log::Error(text.c_str());
        }
    }

    //格式化 hostfxr 返回码。
    std::string FormatHostError(const char* action, int result)
    {
        std::ostringstream stream;
        stream << action << " failed. hostfxr result: 0x" << std::hex << result;
        return stream.str();
    }

    //判断 hostfxr 返回码是否成功。
    bool IsHostSuccess(int result)
    {
        return result >= 0;
    }

    //可选字符串为空时返回空指针。
    const char_t* OptionalStringT(const string_t& value)
    {
        return value.empty() ? nullptr : value.c_str();
    }

    //解析并缓存 hostfxr。
    bool LoadHostfxr(const EditorClrHostConfig& config, void*& hostfxrLibrary, std::string& hostfxrPath, std::string& lastError)
    {
        if (hostfxrLibrary != nullptr
            && FuncInitForConfig != nullptr
            && FuncGetDelegate != nullptr
            && FuncClose != nullptr
            && FuncSetErrorWriter != nullptr)
        {
            return true;
        }

        string_t dotnetRoot = ToStringT(config.dotnetRoot);
        string_t componentAssemblyPath = ToStringT(config.componentAssemblyPath);

        get_hostfxr_parameters parameters{};
        parameters.size = sizeof(get_hostfxr_parameters);
        parameters.assembly_path = OptionalStringT(componentAssemblyPath);
        parameters.dotnet_root = OptionalStringT(dotnetRoot);

        std::vector<char_t> buffer(4096);
        size_t bufferSize = buffer.size();
        int result = get_hostfxr_path(buffer.data(), &bufferSize, &parameters);
        if (result != 0 && bufferSize > buffer.size())
        {
            buffer.resize(bufferSize);
            result = get_hostfxr_path(buffer.data(), &bufferSize, &parameters);
        }

        if (result != 0)
        {
            lastError = FormatHostError("get_hostfxr_path", result);
            return false;
        }

        hostfxrPath = FromStringT(buffer.data());
        DynamicLibrary library = LoadDynamicLibrary(ToPathT(buffer.data()));
        if (library.handle == nullptr)
        {
            lastError = "Failed to load hostfxr: " + hostfxrPath;
            return false;
        }

        FuncInitForConfig = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(GetDynamicLibrarySymbol(library, "hostfxr_initialize_for_runtime_config"));
        FuncGetDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(GetDynamicLibrarySymbol(library, "hostfxr_get_runtime_delegate"));
        FuncClose = reinterpret_cast<hostfxr_close_fn>(GetDynamicLibrarySymbol(library, "hostfxr_close"));
        FuncSetErrorWriter = reinterpret_cast<hostfxr_set_error_writer_fn>(GetDynamicLibrarySymbol(library, "hostfxr_set_error_writer"));

        if (FuncInitForConfig == nullptr || FuncGetDelegate == nullptr || FuncClose == nullptr || FuncSetErrorWriter == nullptr)
        {
            lastError = "Failed to load required hostfxr exports.";
            return false;
        }

        hostfxrLibrary = library.handle;
        return true;
    }

    //初始化 .NET Core 并获取加载函数。
    load_assembly_and_get_function_pointer_fn GetDotnetLoadAssembly(const char_t* runtimeConfigPath,
        const char_t* dotnetRoot,
        std::string& lastError)
    {
        hostfxr_initialize_parameters parameters{};
        parameters.size = sizeof(hostfxr_initialize_parameters);
        parameters.host_path = nullptr;
        parameters.dotnet_root = dotnetRoot;

        hostfxr_handle context = nullptr;
        int result = FuncInitForConfig(runtimeConfigPath, dotnetRoot ? &parameters : nullptr, &context);
        if (!IsHostSuccess(result) || context == nullptr)
        {
            lastError = FormatHostError("hostfxr_initialize_for_runtime_config", result);
            if (context != nullptr) FuncClose(context);
            return nullptr;
        }

        void* loadAssembly = nullptr;
        result = FuncGetDelegate(context, hdt_load_assembly_and_get_function_pointer, &loadAssembly);
        if (!IsHostSuccess(result) || loadAssembly == nullptr)
        {
            lastError = FormatHostError("hostfxr_get_runtime_delegate", result);
            FuncClose(context);
            return nullptr;
        }

        FuncClose(context);
        return reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAssembly);
    }
}

EditorClrHost::~EditorClrHost()
{
    Shutdown();
}

bool EditorClrHost::Initialize(const EditorClrHostConfig& config)
{
    if (IsInitialized()) return true;

    lastError.clear();
    std::string runtimeConfigPath = ToCleanPath(config.runtimeConfigPath);
    if (runtimeConfigPath.empty() || !std::filesystem::exists(runtimeConfigPath))
    {
        lastError = "Editor CLR runtimeconfig does not exist: " + runtimeConfigPath;
        Log::Error(lastError.c_str());
        return false;
    }

    if (!LoadHostfxr(config, hostfxrLibrary, hostfxrPath, lastError))
    {
        Log::Error(lastError.c_str());
        return false;
    }

    FuncSetErrorWriter(&WriteHostfxrError);

    string_t runtimeConfigHostPath = ToStringT(runtimeConfigPath);
    string_t dotnetRoot = ToStringT(config.dotnetRoot);
    load_assembly_and_get_function_pointer_fn loadAssembly = GetDotnetLoadAssembly(
        runtimeConfigHostPath.c_str(),
        OptionalStringT(dotnetRoot),
        lastError);

    if (loadAssembly == nullptr)
    {
        Log::Error(lastError.c_str());
        Shutdown();
        return false;
    }

    LoadAssemblyAndGetFunction = reinterpret_cast<void*>(loadAssembly);
    Log::Info("Editor CLR host initialized.");
    return true;
}

void EditorClrHost::Shutdown()
{
    LoadAssemblyAndGetFunction = nullptr;

    if (FuncSetErrorWriter != nullptr)
    {
        FuncSetErrorWriter(nullptr);
    }
}

bool EditorClrHost::IsInitialized() const
{
    return LoadAssemblyAndGetFunction != nullptr;
}

const std::string& EditorClrHost::GetHostfxrPath() const
{
    return hostfxrPath;
}

const std::string& EditorClrHost::GetLastError() const
{
    return lastError;
}

bool EditorClrHost::BindFunction(const std::string& assemblyPath,
    const std::string& typeName,
    const std::string& methodName,
    void** functionPointer)
{
    if (functionPointer == nullptr)
    {
        lastError = "Editor CLR function pointer output is null.";
        Log::Error(lastError.c_str());
        return false;
    }

    *functionPointer = nullptr;
    if (!IsInitialized())
    {
        lastError = "Editor CLR host is not initialized.";
        Log::Error(lastError.c_str());
        return false;
    }

    std::string assemblyFilePath = ToCleanPath(assemblyPath);
    if (!std::filesystem::exists(assemblyFilePath))
    {
        lastError = "Editor managed assembly does not exist: " + assemblyFilePath;
        Log::Error(lastError.c_str());
        return false;
    }

    string_t assemblyHostPath = ToStringT(assemblyFilePath);
    string_t typeHostName = ToStringT(typeName);
    string_t methodHostName = ToStringT(methodName);

    load_assembly_and_get_function_pointer_fn loadAssembly =
        reinterpret_cast<load_assembly_and_get_function_pointer_fn>(LoadAssemblyAndGetFunction);

    int result = loadAssembly(assemblyHostPath.c_str(),
        typeHostName.c_str(),
        methodHostName.c_str(),
        UNMANAGEDCALLERSONLY_METHOD,
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
