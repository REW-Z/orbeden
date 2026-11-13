#include "Runtime/Coroutines/ThreadOperation.h"

#include "Application.h"

#include <algorithm>
#include <stdexcept>

std::mutex ThreadOperation::mutex;
std::condition_variable ThreadOperation::changed;
std::deque<std::shared_ptr<ThreadOperation::State>> ThreadOperation::queue;
List<std::thread> ThreadOperation::workers;
List<std::shared_ptr<ThreadOperation::State>> ThreadOperation::active;
bool ThreadOperation::stopping = false;

ThreadOperation::ThreadOperation(std::function<void()> function) : state(std::make_shared<State>())
{
    if (!function)
    {
        throw std::invalid_argument("ThreadOperation requires a function.");
    }
    state->function = std::move(function);
    state->window = Application::Current() ? Application::Current()->GetWindow() : nullptr;
    {
        std::lock_guard lock(mutex);
        if (stopping)
        {
            throw std::logic_error("Thread operations cannot start during shutdown.");
        }
        if (workers.empty())
        {
            uint32 count = std::clamp(std::thread::hardware_concurrency(), 1u, 4u);
            workers.reserve(count);
            for (uint32 index = 0; index < count; ++index)
            {
                workers.emplace_back(WorkerLoop);
            }
        }
        active.push_back(state);
        queue.push_back(state);
    }
    changed.notify_one();
}

void ThreadOperation::WorkerLoop()
{
    while (true)
    {
        std::shared_ptr<State> work;
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [] { return stopping || !queue.empty(); });
            if (queue.empty())
            {
                return;
            }
            work = std::move(queue.front());
            queue.pop_front();
        }
        try
        {
            if (!work->canceled.load(std::memory_order_acquire))
            {
                work->function();
            }
        }
        catch (const std::exception& exception)
        {
            work->error = exception.what();
        }
        catch (...)
        {
            work->error = "Thread function failed with a non-standard exception.";
        }
        work->function = {};
        work->finished.store(true, std::memory_order_release);
        if (work->window)
        {
            work->window->WakeEventLoop();
        }
    }
}

void ThreadOperation::Cancel()
{
    state->canceled.store(true, std::memory_order_release);
}

bool ThreadOperation::IsFinished() const
{
    return state->finished.load(std::memory_order_acquire);
}

bool ThreadOperation::Succeeded() const
{
    return IsFinished() && !state->canceled.load(std::memory_order_acquire) && state->error.empty();
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
    std::lock_guard lock(mutex);
    std::erase_if(active, [](const auto& work) { return work->finished.load(std::memory_order_acquire); });
}

void ThreadOperation::WaitAll()
{
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    changed.notify_all();
    for (auto& worker : workers)
    {
        worker.join();
    }
    std::lock_guard lock(mutex);
    workers.clear();
    active.clear();
    stopping = false;
}
