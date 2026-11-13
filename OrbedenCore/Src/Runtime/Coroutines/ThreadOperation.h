#pragma once

#include "Runtime/Coroutines/CoOperation.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class IWindow;

//在线程池中执行函数的协程等待项
class ThreadOperation final : public CoOperation
{
    struct State
    {
        std::function<void()> function;
        IWindow* window = nullptr;
        std::atomic_bool finished = false;
        std::atomic_bool canceled = false;
        std::string error;
    };

    std::shared_ptr<State> state;
    static std::mutex mutex;
    static std::condition_variable changed;
    static std::deque<std::shared_ptr<State>> queue;
    static List<std::thread> workers;
    static List<std::shared_ptr<State>> active;
    static bool stopping;

    //消费后台函数队列
    static void WorkerLoop();

public:
    //将函数提交到共享线程池
    explicit ThreadOperation(std::function<void()> function);

    //取消尚未开始的后台函数
    void Cancel();

    //查询函数是否结束
    bool IsFinished() const;

    //查询函数是否成功
    bool Succeeded() const;

    //读取后台函数错误
    const std::string& GetError() const;

    //等待后台函数结束
    bool ShouldWait(float64 elapsedSeconds) override;

    //释放已经结束的任务状态
    static void ProcessFinished();

    //完成排队任务并回收工作线程
    static void WaitAll();
};
