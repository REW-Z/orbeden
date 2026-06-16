#pragma once

#include <string>

#include "Application.h"

// 脚本系统启动配置，由引擎核心统一持有。
struct ScriptSystemConfig
{
public:
    // .runtimeconfig.json 文件路径。
    std::string runtimeConfigPath;

    // 可选的 dotnet 根目录，留空时由 nethost 自动查找。
    std::string dotnetRoot;

    // 可选的组件程序集路径，用于辅助 hostfxr 查找运行时。
    std::string componentAssemblyPath;

    // 托管程序集输出目录，通常是 exe 同级 Managed。
    std::string managedDirectory;

    // Orbeden.Runtime.dll 路径。
    std::string runtimeAssemblyPath;
};

// 引擎核心脚本子系统，负责维护进程级 .NET 运行时。
class ScriptSystem : public IEngineSystem
{
private:
    void* hostfxrLibrary = nullptr;
    void* LoadAssemblyAndGetFunction = nullptr;
    void* LoadScriptAssemblyFunction = nullptr;
    void* CreateBehaviourFunction = nullptr;
    void* StartBehaviourFunction = nullptr;
    void* UpdateBehaviourFunction = nullptr;
    void* EndBehaviourFunction = nullptr;
    std::string hostfxrPath;
    std::string lastError;
    std::string managedDirectory;
    std::string runtimeAssemblyPath;
    List<std::string> loadedScriptAssemblies;
    List<uint64> activeScriptHandles;

public:
    ScriptSystem() = default;
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    // 子系统销毁时清理脚本系统状态。
    ~ScriptSystem() override;

    // 使用 runtimeconfig.json 初始化 .NET。
    bool Initialize(const ScriptSystemConfig& config);

    // 清理脚本系统状态，保留 .NET 原生库到进程结束。
    void Shutdown();

    // 每帧更新托管脚本生命周期。
    void Update(World& world, float deltaTime) override;

    // 判断 hostfxr 是否已经初始化成功。
    bool IsInitialized() const;

    // 获取解析到的 hostfxr 动态库路径。
    const std::string& GetHostfxrPath() const;

    // 获取最近一次启动或绑定错误。
    const std::string& GetLastError() const;

    // 加载托管静态方法，并返回可从 C++ 调用的函数指针。
    bool LoadAssemblyFunction(const std::string& assemblyPath,
        const std::string& typeName,
        const std::string& methodName,
        const std::string& delegateTypeName,
        void** functionPointer);

    // 方式一：获取托管静态方法，并转换为指定函数指针类型。
    template<typename T>
    T GetCSharpFunction(const std::string& assemblyPath,
        const std::string& typeName,
        const std::string& methodName)
    {
        T FunctionPointer = nullptr;
        LoadAssemblyFunction(assemblyPath, typeName, methodName, std::string(), FunctionPointer);
        return FunctionPointer;
    }

    // 方式二：绑定托管静态方法到 void** 函数指针。
    bool BindCSharpFunction(const std::string& assemblyPath,
        const std::string& typeName,
        const std::string& methodName,
        void** functionPointer);

    // 加载托管静态方法，并转换为指定的函数指针类型。
    template<typename T>
    bool LoadAssemblyFunction(const std::string& assemblyPath,
        const std::string& typeName,
        const std::string& methodName,
        const std::string& delegateTypeName,
        T& functionPointer)
    {
        void* rawFunctionPointer = nullptr;
        if (!LoadAssemblyFunction(assemblyPath, typeName, methodName, delegateTypeName, &rawFunctionPointer))
        {
            return false;
        }

        functionPointer = reinterpret_cast<T>(rawFunctionPointer);
        return true;
    }

private:
    // 初始化 Orbeden.Runtime 托管入口。
    bool InitializeRuntimeBridge(const ScriptSystemConfig& config);

    // 解析脚本程序集路径。
    std::string ResolveScriptAssemblyPath(const std::string& assemblyName) const;

    // 确保脚本程序集已加载到托管运行时。
    bool EnsureScriptAssemblyLoaded(const std::string& assemblyName);

    // 结束指定托管脚本实例。
    void EndManagedBehaviour(uint64 handle);
};
