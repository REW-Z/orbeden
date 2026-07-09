#pragma once

#include "Defines/types.h"
#include "Editor/EditorClrHost.h"
#include "Runtime/Native/NativeCall.h"

#include <string>

//Editor 进程内游戏播放模式，负责绑定用户 Game DLL 的固定入口。
class EditorPlayMode
{
private:
    using InitializeFn = void(ORBEDEN_NATIVE_CALL*)(void*);
    using ShutdownFn = void(ORBEDEN_NATIVE_CALL*)();
    using UpdateFn = void(ORBEDEN_NATIVE_CALL*)(float);
    using DrawGuiFn = void(ORBEDEN_NATIVE_CALL*)();

    InitializeFn InitializeGame = nullptr;
    ShutdownFn ShutdownGame = nullptr;
    UpdateFn UpdateGame = nullptr;
    DrawGuiFn DrawGameGui = nullptr;
    bool playing = false;
    bool paused = false;
    std::string shadowAssemblyPath;
    std::string lastError;

public:
    //启动 Play-In-Editor。
    bool Start(EditorClrHost& host,
        const std::string& assemblyPath,
        const std::string& gameModuleType,
        const std::string& shadowDirectory,
        const List<std::string>& managedDependencyDirectories);

    //停止 Play-In-Editor。
    void Stop();

    //更新用户游戏脚本。
    void Update(float deltaTime);

    //绘制用户游戏 GUI。
    void DrawGui();

    //判断是否正在播放。
    bool IsPlaying() const;

    //设置播放暂停状态。
    void SetPaused(bool value);

    //判断播放是否暂停。
    bool IsPaused() const;

    //获取 shadow copy 后的程序集路径。
    const std::string& GetShadowAssemblyPath() const;

    //获取最近一次错误。
    const std::string& GetLastError() const;

private:
    //清空入口绑定。
    void ClearBindings();
};
