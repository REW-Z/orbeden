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
};

// 引擎核心脚本子系统，负责维护进程级 .NET 运行时。
class ScriptSystem : public IEngineSystem
{
private:
    void* hostfxrLibrary = nullptr;
    void* hostContext = nullptr;
    void* loadAssemblyAndGetFunctionPointer = nullptr;
    std::string hostfxrPath;
    std::string lastError;

public:
    ScriptSystem() = default;
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    // 子系统销毁时释放 hostfxr 上下文。
    ~ScriptSystem() override;

    // 使用 runtimeconfig.json 初始化 .NET。
    bool Initialize(const ScriptSystemConfig& config);

    // 关闭 hostfxr 上下文并卸载 hostfxr 动态库。
    void Shutdown();

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
};
