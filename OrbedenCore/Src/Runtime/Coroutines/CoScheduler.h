#pragma once

#include "Runtime/Coroutines/CoRoutine.h"

#include <functional>
#include <thread>

//在创建线程上推进协程，宿主身份由脚本系统提供
class CoScheduler
{
private:
    List<CoRoutine> routines;
    uint64 frame = 0;
    std::thread::id mainThread = std::this_thread::get_id();
    bool stopping = false;
    bool updating = false;

    //检查主线程调用
    void CheckThread() const;

    //执行等待检查和下一段协程
    void Advance(const std::shared_ptr<CoRoutine::State>& state, float32 deltaTime, bool first);

    //释放已经结束且不在执行中的协程帧
    void Release(const std::shared_ptr<CoRoutine::State>& state);

public:
    //释放全部协程
    ~CoScheduler();

    //在当前线程创建调度器
    CoScheduler() = default;

    //禁止复制调度器
    CoScheduler(const CoScheduler&) = delete;

    //禁止复制调度器状态
    CoScheduler& operator=(const CoScheduler&) = delete;

    //开始新帧，在任何游戏回调之前调用
    void BeginFrame();

    //登记宿主并立即执行到第一个等待点
    CoRoutine Start(int32 owner, CoRoutine routine);

    //停止指定宿主的一个协程
    void Stop(int32 owner, const CoRoutine& routine);

    //停止指定宿主的全部协程
    void StopOwner(int32 owner);

    //停止并释放全部协程
    void StopAll();

    //恢复本帧到期且宿主活动的协程
    void Update(float32 deltaTime, const std::function<bool(int32)>& isOwnerActive);
};
