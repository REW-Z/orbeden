#include <iostream>
#include "Log.h"

namespace
{
    // 输出日志。
    void Print(const char* level, const char* str)
    {
        std::cout << (level != nullptr ? level : "") << (str != nullptr ? str : "") << '\n';
    }
}

void Log::Info(const char* str)
{
    Print("[Info] ", str);
}

void Log::Warning(const char* str)
{
    Print("[Warning] ", str);
}

void Log::Error(const char* str)
{
    Print("[Error] ", str);
}
