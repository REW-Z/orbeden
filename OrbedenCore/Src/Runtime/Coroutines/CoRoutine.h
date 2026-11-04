#pragma once

#include "Runtime/Coroutines/CoOperation.h"

#include <concepts>
#include <coroutine>
#include <exception>
#include <memory>
#include <string>
#include <utility>

class CoScheduler;

//C++ 协程及其完成句柄；可作为另一个协程的等待项
class CoRoutine final : public CoOperation
{
public:
    //编译器协程协议
    struct promise_type
    {
        std::shared_ptr<CoOperation> operation;
        std::exception_ptr exception;

        //返回协程句柄
        CoRoutine get_return_object();

        //等待 RunCo 启动
        std::suspend_always initial_suspend() noexcept { return {}; }

        //等待调度器释放协程帧
        std::suspend_always final_suspend() noexcept { return {}; }

        //结束协程
        void return_void() noexcept {}

        //保存协程异常
        void unhandled_exception() noexcept { exception = std::current_exception(); }

        //保存等待项；空指针等待下一帧
        std::suspend_always yield_value(std::shared_ptr<CoOperation> value) noexcept;

        //保存按值构造的等待项
        template<std::derived_from<CoOperation> T>
        std::suspend_always yield_value(T value)
        {
            return yield_value(std::make_shared<T>(std::move(value)));
        }

        //协程由调度器统一恢复
        void await_transform() = delete;
    };

private:
    friend class CoScheduler;

    //调度器持有的共享运行状态
    struct State
    {
        std::coroutine_handle<promise_type> handle;
        int32 owner = 0;
        uint64 nextFrame = 0;
        float64 elapsedSeconds = 0.0;
        bool started = false;
        bool executing = false;
        bool finished = false;
        bool stopped = false;
        std::string error;

        //释放尚未提交或已退出调度器的协程帧
        ~State();
    };

    std::shared_ptr<State> state;

    //接管编译器创建的协程帧
    explicit CoRoutine(std::coroutine_handle<promise_type> handle);

public:
    //创建空句柄
    CoRoutine() = default;

    //判断协程是否完成，包括停止和失败
    bool IsFinished() const;

    //判断协程是否被主动停止
    bool WasStopped() const;

    //获取失败信息，成功或尚未失败时为空
    const std::string& GetError() const;

    //等待协程结束
    bool ShouldWait(float64 elapsedSeconds) override;
};

