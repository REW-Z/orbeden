#pragma once

//这是发布给游戏模块的公共头，故意不引 Defines/types.h：
//该头定义的 byte 会和 windows.h 拉进来的 rpcndr.h 冲突。
#include <cstdint>

//日志级别
enum class LogLevel : std::int32_t
{
    Info = 0,
    Warning = 1,
    Error = 2,
};

//日志通道
class Log
{
public:
    static void Info(const char* str);

    static void Warning(const char* str);

    static void Error(const char* str);

    //只写入保留窗口，不打印，供已经自行输出过控制台的宿主使用
    static void Append(LogLevel level, const char* str);

    //设置日志唤醒回调：有新日志写入时调用，供界面侧唤醒消息循环。
    //在任何线程上调用；回调里不要再写日志。启动时注册一次即可。
    static void SetWakeHandler(void (*handler)(void* context), void* context);

    //读取保留窗口的序号范围，返回窗口内的条数
    static std::int32_t GetRange(std::int64_t& oldestRevision, std::int64_t& newestRevision);

    //按序号取出一条日志，返回文本长度；序号已被覆盖时返回 -1
    static std::int32_t CopyEntry(std::int64_t revision, char* text, std::int32_t capacity,
        LogLevel& level, std::int64_t& timestampMilliseconds);

    //统计保留窗口内各级别的条数
    static void GetCounts(std::int32_t counts[3]);

    //清空保留窗口
    static void Clear();
};
