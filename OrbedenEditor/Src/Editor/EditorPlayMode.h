#pragma once

#include "Defines/types.h"
#include "Scripting/ScriptSystem.h"

#include <string>

class ManagedEditorBridge;

//Editor 进程内游戏播放模式，负责启动引擎内置的脚本运行时。
class EditorPlayMode
{
private:
    ScriptSystem* scriptSystem = nullptr;
    std::string shadowAssemblyPath;
    std::string lastError;

public:
    //启动 Play-In-Editor；脚本域入口点由编辑器托管栈提供，与编辑态共用同一份 CoreCS
    bool Start(ScriptSystem& scripts,
        ManagedEditorBridge& bridge,
        const std::string& gameAssemblyPath,
        const std::string& runtimeAssemblyPath,
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
