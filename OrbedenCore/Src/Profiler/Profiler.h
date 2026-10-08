#pragma once

#include "Application.h"
#include "Defines/types.h"

#include <chrono>

//性能采样分类，按采样名前缀推导
enum class ProfileCategory : int32
{
    Other = 0,
    Script = 1,
    Render = 2,
    Physics = 3,
    FileIO = 4,
    Editor = 5,
    Capacity = 6,
};

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

    //以下字段只供按帧采集使用，追加在末尾保持已有字段偏移不变
    int32 nameId = -1;
    int32 nodeId = -1;
    int32 depth = 0;
    int32 frameEventIndex = -1;
    int32 frameEventSlot = -1;
};

//一帧内的一次采样调用
struct ProfileEvent
{
    int32 nameId = -1;
    int32 nodeId = -1;
    int32 parentNodeId = -1;
    int32 category = 0;
    int32 depth = 0;
    int64 startMicroseconds = 0;
    int64 durationMicroseconds = 0;
};

//一帧的采样摘要
struct ProfileFrameSummary
{
    int64 frameIndex = 0;
    int64 deltaTimeMicroseconds = 0;
    int64 categoryMicroseconds[6] = {};
    int64 rootMicroseconds = 0;

    //限帧等待：帧率节流主动睡掉的时间，与"其它未计时间"分开统计
    int64 waitMicroseconds = 0;

    int32 eventCount = 0;
    int32 droppedEventCount = 0;
};

//性能剖析器
class Profiler : public IEngineSystem
{
private:
    static ProfileSample* currentSample;
    static ProfileSample* headSample;
    static ProfileSample* tailSample;

public:
    //应用关闭时写出并清空采样数据
    void OnShutdown() override;

    //开始采样
    static ProfileSample* StartSample(const char* name);

    //保存采样
    static void StoreSample(const char* name, int64 elapsedMicroseconds, ProfileSample* sample);

    //写入剖析日志
    static void WriteProfileLog(const char* path = "./Log/profilerLog.txt");

    //清空采样数据与帧历史
    static void Clear();

    //开始新的一帧
    static void NewFrame();

    //结束当前帧，记录这一帧自身的耗时
    static void EndFrame();

    //累加当前帧的限帧等待时间
    static void AddFrameWait(int64 microseconds);

    //开关按帧采集，停止时保留已采集的数据供查看
    static void SetCapturing(bool value);

    //判断是否正在按帧采集
    static bool IsCapturing();

    //释放采集缓冲并清空帧历史
    static void ReleaseCapture();

    //清空帧历史，不动采样树
    static void ClearFrames();

    //读取帧历史的帧序号范围，返回窗口内的帧数
    static int32 GetFrameRange(int64& oldestFrame, int64& newestFrame);

    //拷贝帧摘要，按帧序号升序，返回写入数量
    static int32 CopyFrameSummaries(ProfileFrameSummary* output, int32 capacity);

    //拷贝指定帧的采样事件，返回写入数量
    static int32 CopyFrameEvents(int64 frameIndex, ProfileEvent* output, int32 capacity);

    //读取名字驻留表的条数
    static int32 GetNameCount();

    //按编号拷贝采样名，返回文本长度
    static int32 CopyName(int32 nameId, char* text, int32 capacity);
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

//按采样名前缀推导分类，名字用前缀标记区间：Script/、Render/、Physics/、FileIO/、Editor/
#define ORBEDEN_PROFILE_JOIN_INNER(a, b) a##b
#define ORBEDEN_PROFILE_JOIN(a, b) ORBEDEN_PROFILE_JOIN_INNER(a, b)
#define PROFILE(name) AutoProfile ORBEDEN_PROFILE_JOIN(profileScope, __LINE__)(name)
