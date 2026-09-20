#include "Log.h"

#include "Defines/types.h"

#include <chrono>
#include <clocale>
#include <cstring>
#include <iostream>
#include <mutex>

#ifdef _WIN32
//不带上这两个宏的话 windows.h 会拉进 rpcndr.h，那里的 byte 和 types.h 冲突
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
    // 保留窗口容量
    constexpr int32 RetainedEntryCapacity = 512;
    constexpr int32 LevelCapacity = 3;

    // 单条日志的文本上限，超长截断；异常堆栈会整段塞进一条日志
    constexpr int32 EntryTextCapacity = 2048;

    // 一条保留日志
    struct RetainedEntry
    {
        LogLevel level = LogLevel::Info;
        int64 timestampMilliseconds = 0;
        int32 length = 0;
        char text[EntryTextCapacity] = {};
    };

    // 日志保留窗口，进程启动即可用，不依赖任何引擎系统
    struct RetainedWindow
    {
        RetainedEntry entries[RetainedEntryCapacity];
        int32 count = 0;
        int64 oldestRevision = 0;
        int64 newestRevision = 0;
        int32 counts[LevelCapacity] = {};
        std::mutex lock;
    };

    // 获取日志保留窗口
    RetainedWindow& GetRetainedWindow()
    {
        static RetainedWindow window;
        return window;
    }

    // 唤醒回调，启动时注册一次
    struct WakeHandler
    {
        void (*handler)(void*) = nullptr;
        void* context = nullptr;
    };

    // 获取唤醒回调
    WakeHandler& GetWakeHandler()
    {
        static WakeHandler value;
        return value;
    }

    // 通知界面侧有新日志
    void NotifyWake()
    {
        WakeHandler& handler = GetWakeHandler();
        if (handler.handler) handler.handler(handler.context);
    }

    // 初始化控制台 UTF-8 输出。
    void UseUtf8ConsoleOutput()
    {
        static std::once_flag once;
        std::call_once(once, []()
            {
#ifdef _WIN32
                SetConsoleOutputCP(CP_UTF8);
                SetConsoleCP(CP_UTF8);
                std::setlocale(LC_ALL, ".UTF-8");
#else
                std::setlocale(LC_ALL, "");
#endif
            });
    }

    // 读取当前墙钟毫秒
    int64 NowMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // 规范化级别下标
    int32 ToLevelIndex(LogLevel level)
    {
        int32 index = static_cast<int32>(level);
        if (index < 0 || index >= LevelCapacity)
        {
            index = 0;
        }

        return index;
    }

    // 拷贝日志文本并截断，返回字节数
    int32 CopyEntryText(char* destination, const char* source)
    {
        if (!source)
        {
            destination[0] = '\0';
            return 0;
        }

        usize length = std::strlen(source);
        if (length > EntryTextCapacity - 1)
        {
            length = EntryTextCapacity - 1;
        }

        std::memcpy(destination, source, length);
        destination[length] = '\0';
        return static_cast<int32>(length);
    }

    // 追加一条日志，调用方需持有窗口锁
    void AppendLocked(RetainedWindow& window, LogLevel level, const char* str)
    {
        RetainedEntry& entry = window.entries[window.newestRevision % RetainedEntryCapacity];

        // 覆盖时把被挤出窗口的那条从计数里扣掉
        if (window.count == RetainedEntryCapacity)
        {
            window.counts[ToLevelIndex(entry.level)]--;
        }

        entry.level = level;
        entry.timestampMilliseconds = NowMilliseconds();
        entry.length = CopyEntryText(entry.text, str);

        window.newestRevision++;
        if (window.count < RetainedEntryCapacity)
        {
            window.count++;
        }
        else
        {
            window.oldestRevision = window.newestRevision - RetainedEntryCapacity;
        }

        window.counts[ToLevelIndex(level)]++;
    }

    // 输出日志并保留
    void PrintAndStore(const char* level, LogLevel value, const char* str)
    {
        {
            // 打印与保留共用一次加锁，多线程日志不会互相穿插
            RetainedWindow& window = GetRetainedWindow();
            std::lock_guard<std::mutex> guard(window.lock);

            UseUtf8ConsoleOutput();
            std::cout << (level != nullptr ? level : "") << (str != nullptr ? str : "") << '\n';

            AppendLocked(window, value, str);
        }

        // 在锁外唤醒：回调里若再写日志也不会自锁
        NotifyWake();
    }
}

void Log::Info(const char* str)
{
    PrintAndStore("[Info] ", LogLevel::Info, str);
}

void Log::Warning(const char* str)
{
    PrintAndStore("[Warning] ", LogLevel::Warning, str);
}

void Log::Error(const char* str)
{
    PrintAndStore("[Error] ", LogLevel::Error, str);
}

// 只写入保留窗口，不打印
void Log::Append(LogLevel level, const char* str)
{
    {
        RetainedWindow& window = GetRetainedWindow();
        std::lock_guard<std::mutex> guard(window.lock);
        AppendLocked(window, level, str);
    }

    NotifyWake();
}

// 设置日志唤醒回调
void Log::SetWakeHandler(void (*handler)(void* context), void* context)
{
    WakeHandler& value = GetWakeHandler();
    value.handler = handler;
    value.context = context;
}

// 读取保留窗口的序号范围
int32 Log::GetRange(int64& oldestRevision, int64& newestRevision)
{
    RetainedWindow& window = GetRetainedWindow();
    std::lock_guard<std::mutex> guard(window.lock);

    oldestRevision = window.oldestRevision;
    newestRevision = window.newestRevision;
    return window.count;
}

// 按序号取出一条日志
int32 Log::CopyEntry(int64 revision, char* text, int32 capacity, LogLevel& level, int64& timestampMilliseconds)
{
    RetainedWindow& window = GetRetainedWindow();
    std::lock_guard<std::mutex> guard(window.lock);

    // 序号已被挤出窗口，调用方需要整体重载
    if (revision < window.oldestRevision || revision >= window.newestRevision)
    {
        return -1;
    }

    const RetainedEntry& entry = window.entries[revision % RetainedEntryCapacity];
    level = entry.level;
    timestampMilliseconds = entry.timestampMilliseconds;

    if (text != nullptr && capacity > 0)
    {
        int32 written = entry.length < capacity - 1 ? entry.length : capacity - 1;
        std::memcpy(text, entry.text, static_cast<usize>(written));
        text[written] = '\0';
    }

    return entry.length;
}

// 统计保留窗口内各级别的条数
void Log::GetCounts(int32 counts[3])
{
    RetainedWindow& window = GetRetainedWindow();
    std::lock_guard<std::mutex> guard(window.lock);

    for (int32 index = 0; index < LevelCapacity; index++)
    {
        counts[index] = window.counts[index];
    }
}

// 清空保留窗口
void Log::Clear()
{
    RetainedWindow& window = GetRetainedWindow();
    std::lock_guard<std::mutex> guard(window.lock);

    // 序号推进一格，让持有旧游标的读取方判定为过期并整体重载
    window.newestRevision++;
    window.oldestRevision = window.newestRevision;
    window.count = 0;

    for (int32 index = 0; index < LevelCapacity; index++)
    {
        window.counts[index] = 0;
    }
}
