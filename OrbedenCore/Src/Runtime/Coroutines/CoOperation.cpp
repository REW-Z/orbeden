#include "Runtime/Coroutines/CoOperation.h"

#include <cmath>
#include <stdexcept>
#include <utility>

Wait::Wait(float64 seconds) : seconds(seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
    {
        throw std::invalid_argument("Wait seconds must be finite and non-negative.");
    }
}

bool Wait::ShouldWait(float64 elapsedSeconds)
{
    return elapsedSeconds < seconds;
}

WaitUntil::WaitUntil(std::function<bool()> predicate) : predicate(std::move(predicate))
{
    if (!this->predicate)
    {
        throw std::invalid_argument("WaitUntil requires a predicate.");
    }
}

bool WaitUntil::ShouldWait(float64 elapsedSeconds)
{
    return !predicate();
}

WaitWhile::WaitWhile(std::function<bool()> predicate) : predicate(std::move(predicate))
{
    if (!this->predicate)
    {
        throw std::invalid_argument("WaitWhile requires a predicate.");
    }
}

bool WaitWhile::ShouldWait(float64 elapsedSeconds)
{
    return predicate();
}
