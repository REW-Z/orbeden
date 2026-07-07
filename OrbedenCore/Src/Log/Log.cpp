#include "Log.h"

#include <clocale>
#include <iostream>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
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

    // 输出日志。
    void Print(const char* level, const char* str)
    {
        UseUtf8ConsoleOutput();
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
