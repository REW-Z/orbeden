#include "Runtime/Coroutines/ThreadOperation.h"

#include "Application.h"

#include <algorithm>
#include <stdexcept>

std::mutex ThreadOperation::mutex;
List<std::shared_ptr<ThreadOperation::State>> ThreadOperation::active;
bool ThreadOperation::stopping = false;

ThreadOperation::ThreadOperation(std::function<void()> function) : state(std::make_shared<State>())
{
    if (!function)
    {
        throw std::invalid_argument("ThreadOperation requires a function.");
    }
    IWindow* window = Application::Current() ? Application::Current()->GetWindow() : nullptr;
    std::lock_guard lock(mutex);
    if (stopping)
    {
        throw std::logic_error("Thread operations cannot start during shutdown.");
    }

    //登记线程状态
    state->function = std::move(function);
    active.push_back(state);
    try
    {
        state->thread = std::thread([state = state, window]()
        {
            try
            {
                state->function();
            }
            catch (const std::exception& exception)
            {
                state->error = exception.what();
            }
            catch (...)
            {
                state->error = "Thread function failed with a non-standard exception.";
            }
            state->function = {};
            state->finished.store(true, std::memory_order_release);
            if (window)
            {
                window->WakeEventLoop();
            }
        });
    }
    catch (...)
    {
        std::erase(active, state);
        throw;
    }
}

bool ThreadOperation::IsFinished() const
{
    return state->finished.load(std::memory_order_acquire);
}

bool ThreadOperation::Succeeded() const
{
    return IsFinished() && state->error.empty();
}

const std::string& ThreadOperation::GetError() const
{
    static const std::string empty;
    return IsFinished() ? state->error : empty;
}

bool ThreadOperation::ShouldWait(float64 elapsedSeconds)
{
    return !IsFinished();
}

void ThreadOperation::ProcessFinished()
{
    List<std::shared_ptr<State>> finished;
    {
        std::lock_guard lock(mutex);
        for (const auto& state : active)
        {
            if (state->finished.load(std::memory_order_acquire))
            {
                finished.push_back(state);
            }
        }
        std::erase_if(active, [&finished](const auto& state)
        {
            return std::find(finished.begin(), finished.end(), state) != finished.end();
        });
    }
    for (const auto& state : finished)
    {
        state->thread.join();
    }
}

void ThreadOperation::WaitAll()
{
    List<std::shared_ptr<State>> pending;
    {
        std::lock_guard lock(mutex);
        stopping = true;
        pending.swap(active);
    }
    for (const auto& state : pending)
    {
        state->thread.join();
    }
    std::lock_guard lock(mutex);
    stopping = false;
}
