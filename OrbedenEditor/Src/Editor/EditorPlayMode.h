#pragma once

#include "Defines/types.h"
#include "Editor/EditorClrHost.h"
#include "Scripting/ScriptSystem.h"

#include <string>

//Editor 进程内游戏播放模式，负责绑定用户 Game DLL 的固定入口。
class EditorPlayMode
{
private:
    ScriptSystem* scriptSystem = nullptr;
    std::string shadowAssemblyPath;
    std::string lastError;

public:
    //装载 CLR 游戏入口并启动 Play-In-Editor
    bool Start(ScriptSystem& scripts,
        EditorClrHost& host,
        const std::string& assemblyPath,
        const std::string& gameModuleType,
        const std::string& shadowDirectory,
        const List<std::string>& managedDependencyDirectories);

    //停止 Play-In-Editor
    void Stop();

    //判断是否正在播放
    bool IsPlaying() const;

    //获取 shadow copy 后的程序集路径。
    const std::string& GetShadowAssemblyPath() const;

    //获取最近一次错误。
    const std::string& GetLastError() const;

private:
    //清空本次 CLR 装载状态
    void ClearBindings();
};
