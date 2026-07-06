#pragma once

#include "Defines/types.h"

// Runtime GUI 原生函数表，传给 C# Runtime 保存。
struct RuntimeGuiApi
{
public:
    void* Label = nullptr;
    void* Button = nullptr;
    void* BeginPanel = nullptr;
    void* EndPanel = nullptr;
    void* Checkbox = nullptr;
    void* InputInt = nullptr;
    void* InputFloat = nullptr;
    void* InputVector3 = nullptr;
    void* InputText = nullptr;
    void* BeginComponentBlock = nullptr;
    void* EndComponentBlock = nullptr;
};

// Runtime GUI 桥接层，当前由 Dear ImGui 实现。
class RuntimeGuiBridge
{
public:
    // 获取 Runtime GUI 原生函数表。
    static RuntimeGuiApi GetApi();
};
