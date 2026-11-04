#include "Runtime/Coroutines/CoRoutine.h"

CoRoutine::CoRoutine(std::coroutine_handle<promise_type> handle)
    : state(std::make_shared<State>())
{
    state->handle = handle;
}

bool CoRoutine::IsFinished() const
{
    return !state || state->finished;
}

bool CoRoutine::WasStopped() const
{
    return state && state->stopped;
}

const std::string& CoRoutine::GetError() const
{
    static const std::string empty;
    return state ? state->error : empty;
}

bool CoRoutine::ShouldWait(float64 elapsedSeconds)
{
    return !IsFinished();
}

CoRoutine CoRoutine::promise_type::get_return_object()
{
    return CoRoutine(std::coroutine_handle<promise_type>::from_promise(*this));
}

std::suspend_always CoRoutine::promise_type::yield_value(std::shared_ptr<CoOperation> value) noexcept
{
    operation = std::move(value);
    return {};
}

CoRoutine::State::~State()
{
    if (handle)
    {
        handle.destroy();
    }
}
