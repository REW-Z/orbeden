#pragma once

#include "Defines/types.h"
#include "Runtime/Native/NativeApiAbi.h"

#pragma pack(push, 8)

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

// Runtime GUI 自由绘制函数表，用于 PFD、仪表等自定义 HUD。
// 颜色使用 ImU32 打包字节序（0xAABBGGRR），角度使用弧度；
// 坐标相对当前绘制窗口的内容区域左上角。
struct RuntimeGuiDrawApi
{
public:
    void* BeginFixedWindow = nullptr;
    void* EndFixedWindow = nullptr;
    void* GetViewportSize = nullptr;
    void* Line = nullptr;
    void* Polyline = nullptr;
    void* Rect = nullptr;
    void* RectFilled = nullptr;
    void* Circle = nullptr;
    void* CircleFilled = nullptr;
    void* Arc = nullptr;
    void* TriangleFilled = nullptr;
    void* Text = nullptr;
    void* GetTextSize = nullptr;
    void* PushClipRect = nullptr;
    void* PopClipRect = nullptr;
};

#pragma pack(pop)

ORBEDEN_ASSERT_NATIVE_API_TABLE(RuntimeGuiApi, 11);
ORBEDEN_ASSERT_NATIVE_API_TABLE(RuntimeGuiExtensionApi, 4);
ORBEDEN_ASSERT_NATIVE_API_TABLE(RuntimeGuiAdvancedApi, 17);
ORBEDEN_ASSERT_NATIVE_API_TABLE(RuntimeGuiDrawApi, 15);

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

    // 获取 Runtime GUI 自由绘制函数表。
    static RuntimeGuiDrawApi GetDrawApi();
};
