#include "Platform/ExecutablePath.h"

#include "Defines/types.h"
#include "FileSystem/Utf8Path.h"

#include <system_error>

//可执行文件自身的位置没有标准 C++ 查询方式，只能向系统要。
//平台差异全部收在本文件：非目标平台直接走启动路径回退，调用方不需要知道任何细节。
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace
{
#if defined(_WIN32)
    //Windows：系统保存着本进程的可执行文件完整路径
    std::filesystem::path GetPlatformExecutablePath()
    {
        List<wchar_t> buffer(MAX_PATH);
        while (true)
        {
            DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) return std::filesystem::path();

            //返回长度等于缓冲区容量说明被截断，换更大的缓冲区重试。
            if (static_cast<usize>(length) < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), length));

            buffer.resize(buffer.size() * 2);
        }
    }
#elif defined(__linux__)
    //Linux：/proc 暴露了本进程的可执行文件
    std::filesystem::path GetPlatformExecutablePath()
    {
        std::error_code code;
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", code);
        return code ? std::filesystem::path() : path;
    }
#else
    //其余平台没有通用的查询方式，直接走启动路径回退。
    //新增目标平台时在这里补一个分支即可，其余代码不用改。
    std::filesystem::path GetPlatformExecutablePath()
    {
        return std::filesystem::path();
    }
#endif

    //按启动路径推导可执行文件所在目录
    std::filesystem::path GetDirectoryFromLaunchPath(const std::string& executablePath)
    {
        if (executablePath.empty()) return std::filesystem::current_path();

        std::filesystem::path path = std::filesystem::absolute(Utf8Path::FromUtf8(executablePath));
        return path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    }
}

//获取可执行文件所在目录
std::filesystem::path ExecutablePath::GetDirectory(const std::string& fallbackExecutablePath)
{
    std::filesystem::path executablePath = GetPlatformExecutablePath();
    if (!executablePath.empty() && executablePath.has_parent_path()) return executablePath.parent_path();

    return GetDirectoryFromLaunchPath(fallbackExecutablePath);
}
