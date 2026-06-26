#include "Runtime/Managed/ScriptSystem.h"

#include "Log/Log.h"
#include "Platform/DynamicLibrary.h"
#include "Runtime/Gui/RuntimeGuiBridge.h"
#include "Runtime/Managed/ScriptComponentBinds.h"
#include "Runtime/Object/ScriptsComponent.h"

#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <vector>

using string_t = std::basic_string<char_t>;

namespace
{
    // 保存 hostfxr exports 的全局函数指针。
    hostfxr_initialize_for_runtime_config_fn FuncInitForConfig = nullptr;
    hostfxr_get_runtime_delegate_fn FuncGetDelegate = nullptr;
    hostfxr_close_fn FuncClose = nullptr;
    hostfxr_set_error_writer_fn FuncSetErrorWriter = nullptr;

    using ManagedInitializeRuntimeFn = void(CORECLR_DELEGATE_CALLTYPE*)(void*, void*, void*, void*);
    using ManagedLoadScriptAssemblyFn = uint8(CORECLR_DELEGATE_CALLTYPE*)(const uint8*, int32);
    using ManagedCreateBehaviourFn = uint64(CORECLR_DELEGATE_CALLTYPE*)(const uint8*, int32, EnsId);
    using ManagedStartBehaviourFn = void(CORECLR_DELEGATE_CALLTYPE*)(uint64);
    using ManagedUpdateBehaviourFn = void(CORECLR_DELEGATE_CALLTYPE*)(uint64, float32);
    using ManagedEndBehaviourFn = void(CORECLR_DELEGATE_CALLTYPE*)(uint64);

    constexpr const char* RuntimeTypeName = "Orbeden.ScriptRuntime, Orbeden.Runtime";
    constexpr const char* RuntimeInitializeMethod = "Initialize";
    constexpr const char* RuntimeLoadScriptAssemblyMethod = "LoadScriptAssembly";
    constexpr const char* RuntimeCreateBehaviourMethod = "CreateBehaviour";
    constexpr const char* RuntimeStartBehaviourMethod = "StartBehaviour";
    constexpr const char* RuntimeUpdateBehaviourMethod = "UpdateBehaviour";
    constexpr const char* RuntimeEndBehaviourMethod = "EndBehaviour";

    // 把普通路径字符串转换为 hostfxr 需要的字符类型。
    string_t ToStringT(const std::string& value)
    {
#if defined(_WIN32)
        std::wstring wideValue = std::filesystem::path(value).wstring();
        return string_t(wideValue.begin(), wideValue.end());
#else
        return value;
#endif
    }

    // 把 hostfxr 字符串转换为 UTF-8 字符串。
    std::string FromStringT(const char_t* value)
    {
        if (value == nullptr) return std::string();

#if defined(_WIN32)
        return std::filesystem::path(reinterpret_cast<const wchar_t*>(value)).generic_string();
#else
        return std::string(value);
#endif
    }

    // 把 hostfxr 字符串转换为标准库路径对象。
    std::filesystem::path ToPathT(const char_t* value)
    {
        if (value == nullptr) return std::filesystem::path();

#if defined(_WIN32)
        return std::filesystem::path(reinterpret_cast<const wchar_t*>(value));
#else
        return std::filesystem::path(value);
#endif
    }

    // 规范化磁盘路径。
    std::string NormalizePath(const std::string& path)
    {
        if (path.empty()) return std::string();
        return std::filesystem::absolute(std::filesystem::path(path)).lexically_normal().generic_string();
    }

    // 把路径转换为不强制绝对化的标准文本。
    std::string NormalizePathText(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    // 输出 hostfxr 内部错误。
    void WriteHostfxrError(const char_t* message)
    {
        std::string text = FromStringT(message);
        if (!text.empty())
        {
            Log::Error(text.c_str());
        }
    }

    // 格式化 hostfxr 返回码。
    std::string FormatHostError(const char* action, int result)
    {
        std::ostringstream stream;
        stream << action << " failed. hostfxr result: 0x" << std::hex << result;
        return stream.str();
    }

    // 判断 hostfxr 返回码是否成功。
    bool IsHostSuccess(int result)
    {
        return result >= 0;
    }

    // 可选字符串为空时返回空指针。
    const char_t* OptionalStringT(const string_t& value)
    {
        return value.empty() ? nullptr : value.c_str();
    }

    // 解析并缓存 hostfxr 的必要导出函数。
    bool LoadHostfxr(const ScriptSystemConfig& config, void*& hostfxrLibrary, std::string& hostfxrPath, std::string& lastError)
    {
        if (hostfxrLibrary != nullptr
            && FuncInitForConfig != nullptr
            && FuncGetDelegate != nullptr
            && FuncClose != nullptr
            && FuncSetErrorWriter != nullptr)
        {
            return true;
        }

        // 组装 nethost 查询参数，dotnetRoot 和 assemblyPath 都允许为空。
        string_t dotnetRoot = ToStringT(config.dotnetRoot);
        string_t componentAssemblyPath = ToStringT(config.componentAssemblyPath);

        get_hostfxr_parameters parameters{};
        parameters.size = sizeof(get_hostfxr_parameters);
        parameters.assembly_path = OptionalStringT(componentAssemblyPath);
        parameters.dotnet_root = OptionalStringT(dotnetRoot);

        // 通过 nethost 查找当前机器上可用的 hostfxr 动态库。
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

        // 加载 hostfxr；不要在 Shutdown 中卸载 .NET 原生库。
        hostfxrPath = FromStringT(buffer.data());
        DynamicLibrary library = LoadDynamicLibrary(ToPathT(buffer.data()));
        if (library.handle == nullptr)
        {
            lastError = "Failed to load hostfxr: " + hostfxrPath;
            return false;
        }

        // 获取 DLL 加载模式需要的 hostfxr exports。
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

    // 初始化 .NET Core 并获取加载 C# DLL 的函数指针。
    load_assembly_and_get_function_pointer_fn GetDotnetLoadAssembly(const char_t* runtimeConfigPath,
        const char_t* dotnetRoot,
        std::string& lastError)
    {
        // 有显式 dotnetRoot 时才传初始化参数，保持官方示例的默认路径简洁。
        hostfxr_initialize_parameters parameters{};
        parameters.size = sizeof(hostfxr_initialize_parameters);
        parameters.host_path = nullptr;
        parameters.dotnet_root = dotnetRoot;

        const bool hasParameters = dotnetRoot != nullptr;

        // 按 runtimeconfig.json 初始化 .NET runtime。
        hostfxr_handle context = nullptr;
        int result = FuncInitForConfig(runtimeConfigPath, hasParameters ? &parameters : nullptr, &context);
        if (!IsHostSuccess(result) || context == nullptr)
        {
            lastError = FormatHostError("hostfxr_initialize_for_runtime_config", result);
            if (context != nullptr)
            {
                FuncClose(context);
            }

            return nullptr;
        }

        // 从 hostfxr context 获取 DLL 加载 delegate。
        void* LoadAssemblyAndGetFunctionPointer = nullptr;
        result = FuncGetDelegate(context, hdt_load_assembly_and_get_function_pointer, &LoadAssemblyAndGetFunctionPointer);
        if (!IsHostSuccess(result) || LoadAssemblyAndGetFunctionPointer == nullptr)
        {
            lastError = FormatHostError("hostfxr_get_runtime_delegate", result);
            FuncClose(context);
            return nullptr;
        }

        // 关闭 hostfxr context；获取到的 runtime delegate 可以继续被 ScriptSystem 缓存使用。
        FuncClose(context);
        return reinterpret_cast<load_assembly_and_get_function_pointer_fn>(LoadAssemblyAndGetFunctionPointer);
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
    managedDirectory.clear();
    runtimeAssemblyPath.clear();
    loadedScriptAssemblies.clear();
    activeScriptHandles.clear();

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

    // 加载 hostfxr 并解析必要导出函数。
    if (!LoadHostfxr(config, hostfxrLibrary, hostfxrPath, lastError))
    {
        Log::Error(lastError.c_str());
        return false;
    }

    // 接管 hostfxr 错误输出，便于初始化失败时进入引擎日志。
    FuncSetErrorWriter(&WriteHostfxrError);

    // 转换 hostfxr 初始化所需路径。
    string_t runtimeConfigHostPath = ToStringT(runtimeConfigPath);
    string_t dotnetRoot = ToStringT(config.dotnetRoot);

    // 初始化 .NET 并获取 load_assembly_and_get_function_pointer。
    load_assembly_and_get_function_pointer_fn LoadAssemblyAndGetFunction = GetDotnetLoadAssembly(
        runtimeConfigHostPath.c_str(),
        OptionalStringT(dotnetRoot),
        lastError);

    if (LoadAssemblyAndGetFunction == nullptr)
    {
        Log::Error(lastError.c_str());
        Shutdown();
        return false;
    }

    // 缓存 DLL 加载 delegate，后续所有 C# 函数绑定都从这里进入。
    this->LoadAssemblyAndGetFunction = reinterpret_cast<void*>(LoadAssemblyAndGetFunction);
    if (!InitializeRuntimeBridge(config))
    {
        Shutdown();
        return false;
    }

    Log::Info("ScriptSystem initialized.");
    return true;
}

void ScriptSystem::Shutdown()
{
    // 先通知所有仍然活着的 C# 脚本结束。
    for (uint64 handle : activeScriptHandles)
    {
        EndManagedBehaviour(handle);
    }
    activeScriptHandles.clear();
    loadedScriptAssemblies.clear();

    // 清空 runtime delegate，但不卸载 hostfxr 或 .NET 原生库。
    LoadAssemblyAndGetFunction = nullptr;
    LoadScriptAssemblyFunction = nullptr;
    CreateBehaviourFunction = nullptr;
    StartBehaviourFunction = nullptr;
    UpdateBehaviourFunction = nullptr;
    EndBehaviourFunction = nullptr;
    managedDirectory.clear();
    runtimeAssemblyPath.clear();

    // 还原 hostfxr 错误回调，避免对象销毁后继续写入引擎日志。
    if (FuncSetErrorWriter != nullptr)
    {
        FuncSetErrorWriter(nullptr);
    }
}

void ScriptSystem::Update(World& world, float deltaTime)
{
    if (!IsInitialized() || CreateBehaviourFunction == nullptr || UpdateBehaviourFunction == nullptr) return;

    List<uint64> currentHandles;
    ManagedCreateBehaviourFn CreateBehaviour = reinterpret_cast<ManagedCreateBehaviourFn>(CreateBehaviourFunction);
    ManagedStartBehaviourFn StartBehaviour = reinterpret_cast<ManagedStartBehaviourFn>(StartBehaviourFunction);
    ManagedUpdateBehaviourFn UpdateBehaviour = reinterpret_cast<ManagedUpdateBehaviourFn>(UpdateBehaviourFunction);

    // 扫描当前 World 上所有脚本槽位。
    world.ForEachComponent<ScriptsComponent>([&](ScriptsComponent* scriptsComponent)
        {
            for (ScriptSlot& slot : scriptsComponent->scripts)
            {
                if (!slot.enabled || slot.assemblyName.empty() || slot.typeName.empty())
                {
                    slot.managedHandle = 0;
                    slot.started = false;
                    continue;
                }

                // 首次运行时加载程序集并创建 C# ScriptBehaviour。
                if (slot.managedHandle == 0)
                {
                    if (!EnsureScriptAssemblyLoaded(slot.assemblyName)) continue;

                    slot.managedHandle = CreateBehaviour(
                        reinterpret_cast<const uint8*>(slot.typeName.data()),
                        static_cast<int32>(slot.typeName.size()),
                        scriptsComponent->GetEnsId());
                    slot.started = false;
                }

                if (slot.managedHandle == 0) continue;

                // 第一次创建成功后调用 OnStart。
                if (!slot.started && StartBehaviour != nullptr)
                {
                    StartBehaviour(slot.managedHandle);
                    slot.started = true;
                }

                UpdateBehaviour(slot.managedHandle, deltaTime);
                currentHandles.push_back(slot.managedHandle);
            }
        });

    // 对已经离开 World 或被禁用的旧脚本调用 OnEnd。
    for (uint64 handle : activeScriptHandles)
    {
        if (std::find(currentHandles.begin(), currentHandles.end(), handle) == currentHandles.end())
        {
            EndManagedBehaviour(handle);
        }
    }

    activeScriptHandles = currentHandles;
}

bool ScriptSystem::IsInitialized() const
{
    return LoadAssemblyAndGetFunction != nullptr;
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

    // 转换 load_assembly_and_get_function_pointer 需要的托管符号。
    string_t assemblyHostPath = ToStringT(normalizedAssemblyPath);
    string_t typeHostName = ToStringT(typeName);
    string_t methodHostName = ToStringT(methodName);
    string_t delegateHostTypeName = ToStringT(delegateTypeName);
    const char_t* delegateTypeNameValue = delegateHostTypeName.empty()
        ? UNMANAGEDCALLERSONLY_METHOD
        : delegateHostTypeName.c_str();

    // 方式二的底层路径：直接把 C# 静态函数绑定到 void**。
    load_assembly_and_get_function_pointer_fn FuncLoadAssemblyAndGetFunction =
        reinterpret_cast<load_assembly_and_get_function_pointer_fn>(LoadAssemblyAndGetFunction);

    int result = FuncLoadAssemblyAndGetFunction(assemblyHostPath.c_str(),
        typeHostName.c_str(),
        methodHostName.c_str(),
        delegateTypeNameValue,
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

bool ScriptSystem::BindCSharpFunction(const std::string& assemblyPath,
    const std::string& typeName,
    const std::string& methodName,
    void** functionPointer)
{
    // 方式二：调用方显式传入 void**，常用于先 typedef 再绑定。
    return LoadAssemblyFunction(assemblyPath, typeName, methodName, std::string(), functionPointer);
}

bool ScriptSystem::InitializeRuntimeBridge(const ScriptSystemConfig& config)
{
    // 确定 Orbeden.Runtime 和 Managed 目录。
    runtimeAssemblyPath = NormalizePath(config.runtimeAssemblyPath.empty() ? config.componentAssemblyPath : config.runtimeAssemblyPath);
    if (runtimeAssemblyPath.empty() || !std::filesystem::exists(runtimeAssemblyPath))
    {
        lastError = "Orbeden.Runtime assembly does not exist: " + runtimeAssemblyPath;
        Log::Error(lastError.c_str());
        return false;
    }

    managedDirectory = config.managedDirectory.empty()
        ? NormalizePathText(std::filesystem::path(runtimeAssemblyPath).parent_path())
        : NormalizePath(config.managedDirectory);

    // 绑定 Runtime 初始化入口和脚本生命周期入口。
    ManagedInitializeRuntimeFn InitializeRuntime = nullptr;
    if (!BindCSharpFunction(runtimeAssemblyPath, RuntimeTypeName, RuntimeInitializeMethod, reinterpret_cast<void**>(&InitializeRuntime))) return false;
    if (!BindCSharpFunction(runtimeAssemblyPath, RuntimeTypeName, RuntimeLoadScriptAssemblyMethod, &LoadScriptAssemblyFunction)) return false;
    if (!BindCSharpFunction(runtimeAssemblyPath, RuntimeTypeName, RuntimeCreateBehaviourMethod, &CreateBehaviourFunction)) return false;
    if (!BindCSharpFunction(runtimeAssemblyPath, RuntimeTypeName, RuntimeStartBehaviourMethod, &StartBehaviourFunction)) return false;
    if (!BindCSharpFunction(runtimeAssemblyPath, RuntimeTypeName, RuntimeUpdateBehaviourMethod, &UpdateBehaviourFunction)) return false;
    if (!BindCSharpFunction(runtimeAssemblyPath, RuntimeTypeName, RuntimeEndBehaviourMethod, &EndBehaviourFunction)) return false;

    // 把 Runtime 原生函数表传给 C# Runtime。
    RuntimeGuiApi runtimeGuiApi = RuntimeGuiBridge::GetApi();
    EnsBind ensBind = EnsBind::Create();
    SpaceComponentBind spaceComponentBind = SpaceComponentBind::Create();
    StaticMeshRendererBind staticMeshRendererBind = StaticMeshRendererBind::Create();
    InitializeRuntime(&runtimeGuiApi, &ensBind, &spaceComponentBind, &staticMeshRendererBind);
    return true;
}

std::string ScriptSystem::ResolveScriptAssemblyPath(const std::string& assemblyName) const
{
    if (assemblyName.empty()) return std::string();

    std::filesystem::path assemblyPath(assemblyName);
    if (!assemblyPath.has_extension())
    {
        assemblyPath += ".dll";
    }

    if (!assemblyPath.is_absolute())
    {
        assemblyPath = std::filesystem::path(managedDirectory) / assemblyPath;
    }

    return NormalizePathText(assemblyPath);
}

bool ScriptSystem::EnsureScriptAssemblyLoaded(const std::string& assemblyName)
{
    std::string assemblyPath = ResolveScriptAssemblyPath(assemblyName);
    if (assemblyPath.empty()) return false;
    if (std::find(loadedScriptAssemblies.begin(), loadedScriptAssemblies.end(), assemblyPath) != loadedScriptAssemblies.end()) return true;

    if (!std::filesystem::exists(assemblyPath))
    {
        lastError = "Script assembly does not exist: " + assemblyPath;
        Log::Error(lastError.c_str());
        return false;
    }

    ManagedLoadScriptAssemblyFn LoadScriptAssembly = reinterpret_cast<ManagedLoadScriptAssemblyFn>(LoadScriptAssemblyFunction);
    if (!LoadScriptAssembly || LoadScriptAssembly(reinterpret_cast<const uint8*>(assemblyPath.data()), static_cast<int32>(assemblyPath.size())) == 0)
    {
        lastError = "Script assembly load failed: " + assemblyPath;
        Log::Error(lastError.c_str());
        return false;
    }

    loadedScriptAssemblies.push_back(assemblyPath);
    return true;
}

void ScriptSystem::EndManagedBehaviour(uint64 handle)
{
    if (handle == 0 || EndBehaviourFunction == nullptr) return;

    ManagedEndBehaviourFn EndBehaviour = reinterpret_cast<ManagedEndBehaviourFn>(EndBehaviourFunction);
    EndBehaviour(handle);
}
