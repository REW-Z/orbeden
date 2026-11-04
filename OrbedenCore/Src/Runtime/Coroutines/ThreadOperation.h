#pragma once

#include "Runtime/Coroutines/CoOperation.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

//在线程中执行函数的协程等待项
class ThreadOperation final : public CoOperation
{
    struct State
    {
        std::thread thread;
        std::function<void()> function;
        std::atomic_bool finished = false;
        std::string error;
    };

    std::shared_ptr<State> state;
    static std::mutex mutex;
    static List<std::shared_ptr<State>> active;
    static bool stopping;

public:
    //立即启动一条独立线程执行函数
    explicit ThreadOperation(std::function<void()> function);

    //查询函数是否结束
    bool IsFinished() const;

    //查询函数是否成功
    bool Succeeded() const;

    //读取后台函数错误
    const std::string& GetError() const;

    //等待后台函数结束
    bool ShouldWait(float64 elapsedSeconds) override;

    //回收已经结束的线程
    static void ProcessFinished();

    //等待并回收全部线程
    static void WaitAll();
};
