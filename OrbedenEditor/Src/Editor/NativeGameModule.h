#pragma once

#include "Defines/types.h"
#include "Platform/DynamicLibrary.h"

#include <string>

//Editor 内的游戏原生模块，负责 shadow copy、ABI 校验和安全回滚。
class NativeGameModule
{
private:
    DynamicLibrary library;
    std::string shadowPath;
    uint64 shadowVersion = 0;

    //装载并验证一个 shadow DLL。
    bool LoadCandidate(const std::string& sourcePath, const std::string& shadowDirectory, std::string& error);

public:
    ~NativeGameModule();

    //用新 DLL 替换当前模块；失败时恢复上一版本。
    bool Reload(const std::string& sourcePath,
        const std::string& shadowDirectory,
        const List<std::string>& requiredTypes,
        std::string& error);

    //在模块实例均已清空后注销元数据并卸载 DLL。
    bool Unload(std::string& error);

    //判断当前是否已装载模块。
    bool IsLoaded() const;

    //获取当前 shadow DLL 路径。
    const std::string& GetShadowPath() const;
};
