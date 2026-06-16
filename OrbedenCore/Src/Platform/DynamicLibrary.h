#pragma once

#include <filesystem>

// 原生动态库句柄，具体平台含义由 Platform 层隐藏。
struct DynamicLibrary
{
public:
    void* handle = nullptr;
};

// 加载原生动态库。
DynamicLibrary LoadDynamicLibrary(const std::filesystem::path& path);

// 获取原生动态库导出符号。
void* GetDynamicLibrarySymbol(DynamicLibrary library, const char* name);
