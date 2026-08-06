#include "Platform/DynamicLibrary.h"

#include "FileSystem/Utf8Path.h"

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

    //转换 DLL 路径
#if defined(_WIN32)
    std::wstring widePath = path.wstring();
    library.handle = LoadLibraryW(widePath.c_str());
#else
    std::string nativePath = Utf8Path::ToUtf8(path);
    library.handle = dlopen(nativePath.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif

    return library;
}

void* GetDynamicLibrarySymbol(DynamicLibrary library, const char* name)
{
    if (library.handle == nullptr || name == nullptr) return nullptr;

    //查询导出符号
#if defined(_WIN32)
    return GetProcAddress(library.handle, name);
#else
    return dlsym(library.handle, name);
#endif
}
