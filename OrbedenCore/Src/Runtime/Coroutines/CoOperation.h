#pragma once

#include "Defines/types.h"

#include <functional>

//协程等待项；elapsedSeconds 是本次等待经过的有效更新秒数
class CoOperation
{
public:
    //释放等待项
    virtual ~CoOperation() = default;

    //返回 true 时继续等待，返回 false 时恢复协程
    virtual bool ShouldWait(float64 elapsedSeconds) = 0;
};

//等待指定的有效更新秒数
class Wait final : public CoOperation
{
private:
    float64 seconds;

public:
    //保存等待秒数
    explicit Wait(float64 seconds);

    //检查等待时间
    bool ShouldWait(float64 elapsedSeconds) override;
};

//等待条件成立
class WaitUntil final : public CoOperation
{
private:
    std::function<bool()> predicate;

public:
    //保存恢复条件
    explicit WaitUntil(std::function<bool()> predicate);

    //检查恢复条件
    bool ShouldWait(float64 elapsedSeconds) override;
};

//等待条件不再成立
class WaitWhile final : public CoOperation
{
private:
    std::function<bool()> predicate;

public:
    //保存等待条件
    explicit WaitWhile(std::function<bool()> predicate);

    //检查等待条件
    bool ShouldWait(float64 elapsedSeconds) override;
};
