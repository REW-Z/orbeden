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

// Runtime GUI 增量函数表，追加到原生 API 末尾以兼容旧模块布局。
struct RuntimeGuiExtensionApi
{
public:
    void* BeginCollapsibleComponentBlock = nullptr;
    void* BeginCombo = nullptr;
    void* EndCombo = nullptr;
    void* Selectable = nullptr;
};

// Runtime GUI 高级控件函数表。
struct RuntimeGuiAdvancedApi
{
public:
    void* Separator = nullptr;
    void* SameLine = nullptr;
    void* BeginTable = nullptr;
    void* EndTable = nullptr;
    void* TableSetupColumn = nullptr;
    void* TableHeadersRow = nullptr;
    void* TableNextRow = nullptr;
    void* TableSetColumnIndex = nullptr;
    void* Selectable = nullptr;
    void* IsItemDoubleClicked = nullptr;
    void* BeginPopupContextItem = nullptr;
    void* BeginPopupContextWindow = nullptr;
    void* EndPopup = nullptr;
    void* MenuItem = nullptr;
    void* SetClipboardText = nullptr;
    void* BeginDisabled = nullptr;
    void* EndDisabled = nullptr;
};

// Runtime GUI 桥接层，当前由 Dear ImGui 实现。
class RuntimeGuiBridge
{
public:
    // 获取 Runtime GUI 原生函数表。
    static RuntimeGuiApi GetApi();

    // 获取 Runtime GUI 增量函数表。
    static RuntimeGuiExtensionApi GetExtensionApi();

    // 获取 Runtime GUI 高级控件函数表。
    static RuntimeGuiAdvancedApi GetAdvancedApi();
};
