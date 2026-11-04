#include "Runtime/Coroutines/CoScheduler.h"

#include "Log/Log.h"

#include <algorithm>
#include <stdexcept>

CoScheduler::~CoScheduler()
{
    StopAll();
}

void CoScheduler::CheckThread() const
{
    if (std::this_thread::get_id() != mainThread)
    {
        throw std::logic_error("CoRoutines must be accessed on the main thread.");
    }
}

void CoScheduler::BeginFrame()
{
    CheckThread();
    ++frame;
}

CoRoutine CoScheduler::Start(int32 owner, CoRoutine routine)
{
    CheckThread();
    if (stopping || !routine.state || routine.state->started)
    {
        throw std::logic_error("A coroutine must be started once in an active runtime.");
    }

    //登记运行状态
    auto state = routine.state;
    state->owner = owner;
    state->started = true;
    state->nextFrame = frame + 1;
    routines.push_back(routine);

    //执行协程首段
    Advance(state, 0.0f, true);
    return routine;
}

void CoScheduler::Stop(int32 owner, const CoRoutine& routine)
{
    CheckThread();
    auto state = routine.state;
    if (!state || !state->started || state->owner != owner || state->finished)
    {
        return;
    }

    state->stopped = true;
    state->finished = true;
    Release(state);
}

void CoScheduler::StopOwner(int32 owner)
{
    CheckThread();
    bool previous = std::exchange(stopping, true);
    usize count = routines.size();
    for (usize index = 0; index < count; ++index)
    {
        CoRoutine routine = routines[index];
        Stop(owner, routine);
    }
    stopping = previous;
}

void CoScheduler::StopAll()
{
    CheckThread();
    bool previous = std::exchange(stopping, true);
    usize count = routines.size();
    for (usize index = 0; index < count; ++index)
    {
        CoRoutine routine = routines[index];
        Stop(routine.state->owner, routine);
    }
    stopping = previous;
    if (!updating && !previous)
    {
        routines.clear();
    }
}

void CoScheduler::Release(const std::shared_ptr<CoRoutine::State>& state)
{
    if (!state->finished || state->executing || !state->handle)
    {
        return;
    }

    state->executing = true;
    bool previous = std::exchange(stopping, true);
    auto handle = std::exchange(state->handle, {});
    handle.destroy();
    stopping = previous;
    state->executing = false;
}

void CoScheduler::Advance(const std::shared_ptr<CoRoutine::State>& state, float32 deltaTime, bool first)
{
    state->executing = true;
    try
    {
        //检查当前等待项
        bool waiting = false;
        if (!first)
        {
            state->elapsedSeconds += deltaTime;
            auto operation = state->handle.promise().operation;
            waiting = operation && operation->ShouldWait(state->elapsedSeconds);
        }

        //执行下一段协程
        if (!waiting && !state->finished)
        {
            state->handle.promise().operation.reset();
            if (!state->finished)
            {
                state->handle.resume();
            }
            if (state->handle.done())
            {
                state->finished = true;
                if (state->handle.promise().exception)
                {
                    std::rethrow_exception(state->handle.promise().exception);
                }
            }
            state->elapsedSeconds = 0.0;
            state->nextFrame = frame + 1;
        }
    }
    catch (const std::exception& exception)
    {
        state->finished = true;
        state->error = exception.what();
    }
    catch (...)
    {
        state->finished = true;
        state->error = "Non-standard coroutine exception.";
    }
    state->executing = false;

    //报告错误并释放结束状态
    if (!state->error.empty())
    {
        Log::Error(("C++ coroutine on Script " + std::to_string(state->owner) + " failed: " + state->error).c_str());
    }
    Release(state);
}

void CoScheduler::Update(float32 deltaTime, const std::function<bool(int32)>& isOwnerActive)
{
    CheckThread();
    if (updating)
    {
        throw std::logic_error("CoRoutine updates cannot be nested.");
    }

    //推进本帧已有协程
    updating = true;
    usize count = routines.size();
    for (usize index = 0; index < count; ++index)
    {
        auto state = routines[index].state;
        if (!state->finished && state->nextFrame <= frame && isOwnerActive(state->owner))
        {
            Advance(state, deltaTime, false);
        }
    }
    updating = false;

    //移除结束项
    std::erase_if(routines, [](const CoRoutine& routine) { return routine.IsFinished(); });
}
