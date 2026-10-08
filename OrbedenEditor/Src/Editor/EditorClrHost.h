#pragma once

#include <string>

//Editor CLR 启动配置。
struct EditorClrHostConfig
{
public:
    std::string runtimeConfigPath;
    std::string overrideDotnetRoot;
    std::string componentAssemblyPath;
};

//Editor 专用 CoreCLR 宿主。
class EditorClrHost
{
private:
    void* hostfxrLibrary = nullptr;
    void* LoadAssemblyAndGetFunction = nullptr;
    std::string hostfxrPath;
    std::string lastError;

public:
    EditorClrHost() = default;
    EditorClrHost(const EditorClrHost&) = delete;
    EditorClrHost& operator=(const EditorClrHost&) = delete;

    //关闭 Editor CLR 宿主。
    ~EditorClrHost();

    //使用 runtimeconfig.json 初始化 CLR。
    bool Initialize(const EditorClrHostConfig& config);

    //清空 CLR 绑定状态。
    void Shutdown();

    //判断 CLR 是否已经初始化。
    bool IsInitialized() const;

    //获取 hostfxr 路径。
    const std::string& GetHostfxrPath() const;

    //获取最近一次错误。
    const std::string& GetLastError() const;

    //绑定托管静态函数。
    bool BindFunction(const std::string& assemblyPath,
        const std::string& typeName,
        const std::string& methodName,
        void** functionPointer);
};
