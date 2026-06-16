#include "Platform/DynamicLibrary.h"

#include <string>

#if defined(_WIN32)
extern "C"
{
    __declspec(dllimport) void* __stdcall LoadLibraryW(const wchar_t* fileName);
    __declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
}
#else
#include <dlfcn.h>
#endif

DynamicLibrary LoadDynamicLibrary(const std::filesystem::path& path)
{
    DynamicLibrary library{};

    // Windows 使用宽字符路径，避免本地编码影响 DLL 查找。
#if defined(_WIN32)
    std::wstring widePath = path.wstring();
    library.handle = LoadLibraryW(widePath.c_str());
#else
    std::string nativePath = path.string();
    library.handle = dlopen(nativePath.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif

    return library;
}

void* GetDynamicLibrarySymbol(DynamicLibrary library, const char* name)
{
    if (library.handle == nullptr || name == nullptr) return nullptr;

    // 导出符号查询必须走系统动态库 API，C/C++ 标准库没有等价能力。
#if defined(_WIN32)
    return GetProcAddress(library.handle, name);
#else
    return dlsym(library.handle, name);
#endif
}
