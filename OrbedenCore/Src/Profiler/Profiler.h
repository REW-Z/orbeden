#pragma once

#include "Defines/types.h"

#include <chrono>

//性能采样数据
struct ProfileSample
{
public:
    const char* name = nullptr;
    int64 timeTotalMicroseconds = 0;
    int32 invokeCount = 0;

    ProfileSample* parent = nullptr;
    ProfileSample* firstChild = nullptr;
    ProfileSample* lastChild = nullptr;
    ProfileSample* next = nullptr;
};

//性能剖析器
class Profiler
{
private:
    static ProfileSample* currentSample;
    static ProfileSample* headSample;
    static ProfileSample* tailSample;

public:
    //开始采样
    static ProfileSample* StartSample(const char* name);

    //保存采样
    static void StoreSample(const char* name, int64 elapsedMicroseconds, ProfileSample* sample);

    //写入剖析日志
    static void WriteProfileLog(const char* path = "./Log/profilerLog.txt");

    //清空采样数据
    static void Clear();
};

//自动作用域采样
class AutoProfile
{
private:
    const char* name = nullptr;
    ProfileSample* sample = nullptr;
    std::chrono::steady_clock::time_point startTime;

public:
    //开始作用域采样
    AutoProfile(const char* str);

    //结束作用域采样
    ~AutoProfile();
};

#define ORBEDEN_PROFILE_JOIN_INNER(a, b) a##b
#define ORBEDEN_PROFILE_JOIN(a, b) ORBEDEN_PROFILE_JOIN_INNER(a, b)
#define PROFILE(name) AutoProfile ORBEDEN_PROFILE_JOIN(profileScope, __LINE__)(name)
