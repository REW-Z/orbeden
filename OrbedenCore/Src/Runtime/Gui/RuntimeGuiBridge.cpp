#include "Runtime/Gui/RuntimeGuiBridge.h"

#include "Runtime/Native/NativeCall.h"
#include "Runtime/EngineTypes.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
    constexpr float32 MinPanelWidth = 120.0f;
    constexpr float32 MinPanelHeight = 80.0f;

    // 从 C# 传入的 UTF-8 字节读取临时字符串。
    std::string ReadUtf8Text(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(length));
    }

    // 将当前 GUI 面板限制在主窗口内。
    void ClampCurrentPanel()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport) return;

        ImVec2 size = ImGui::GetWindowSize();
        size.x = std::clamp(size.x, MinPanelWidth, std::max(MinPanelWidth, viewport->WorkSize.x));
        size.y = std::clamp(size.y, MinPanelHeight, std::max(MinPanelHeight, viewport->WorkSize.y));

        ImVec2 position = ImGui::GetWindowPos();
        position.x = std::clamp(position.x,
            viewport->WorkPos.x,
            viewport->WorkPos.x + std::max(0.0f, viewport->WorkSize.x - size.x));
        position.y = std::clamp(position.y,
            viewport->WorkPos.y,
            viewport->WorkPos.y + std::max(0.0f, viewport->WorkSize.y - size.y));

        ImGui::SetWindowPos(position, ImGuiCond_Always);
        ImGui::SetWindowSize(size, ImGuiCond_Always);
    }

    // 绘制文本标签。
    void ORBEDEN_NATIVE_CALL RuntimeGuiLabel(const uint8* text, int32 length)
    {
        const char* begin = text && length > 0 ? reinterpret_cast<const char*>(text) : "";
        const char* end = begin + std::max(length, 0);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(begin, end);
        ImGui::PopTextWrapPos();
    }

    // 绘制按钮并返回是否点击。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiButton(const uint8* text, int32 length)
    {
        std::string value = ReadUtf8Text(text, length);
        return ImGui::Button(value.c_str()) ? 1 : 0;
    }

    // 开始一个浮动面板。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiBeginPanel(const uint8* title, int32 length)
    {
        std::string value = ReadUtf8Text(title, length);
        bool open = ImGui::Begin(value.empty() ? "Managed Panel" : value.c_str());
        ClampCurrentPanel();
        return open ? 1 : 0;
    }

    // 结束一个浮动面板。
    void ORBEDEN_NATIVE_CALL RuntimeGuiEndPanel()
    {
        ImGui::End();
    }

    // 开始绘制组件块。
    void ORBEDEN_NATIVE_CALL RuntimeGuiBeginComponentBlock(const uint8* title, int32 length)
    {
        std::string value = ReadUtf8Text(title, length);
        if (value.empty())
        {
            value = "Component";
        }

        ImGui::Spacing();
        ImGui::PushID(value.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::BeginChild("##component", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextUnformatted(value.c_str());
        ImGui::Separator();
    }

    // 结束绘制组件块。
    void ORBEDEN_NATIVE_CALL RuntimeGuiEndComponentBlock()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::PopID();
        ImGui::Spacing();
    }

    // 开始绘制可折叠、可选移除的组件块。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiBeginCollapsibleComponentBlock(const uint8* title,
        int32 titleLength,
        const uint8* id,
        int32 idLength,
        uint8 removable,
        uint8* removeRequested)
    {
        std::string value = ReadUtf8Text(title, titleLength);
        std::string identity = ReadUtf8Text(id, idLength);
        if (value.empty()) value = "Component";
        if (identity.empty()) identity = value;
        if (removeRequested) *removeRequested = 0;

        ImGui::Spacing();
        ImGui::PushID(identity.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::BeginChild("##component", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        bool visible = true;
        bool expanded = removable != 0
            ? ImGui::CollapsingHeader(value.c_str(), &visible, flags)
            : ImGui::CollapsingHeader(value.c_str(), flags);
        if (removeRequested && !visible) *removeRequested = 1;
        if (expanded) ImGui::Separator();
        return expanded ? 1 : 0;
    }

    // 开始绘制下拉选择框。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiBeginCombo(const uint8* label,
        int32 labelLength,
        const uint8* preview,
        int32 previewLength)
    {
        std::string labelText = ReadUtf8Text(label, labelLength);
        std::string previewText = ReadUtf8Text(preview, previewLength);
        return ImGui::BeginCombo(labelText.c_str(), previewText.c_str()) ? 1 : 0;
    }

    // 结束当前下拉选择框。
    void ORBEDEN_NATIVE_CALL RuntimeGuiEndCombo()
    {
        ImGui::EndCombo();
    }

    // 绘制下拉选择项。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiSelectable(const uint8* label, int32 length, uint8 selected)
    {
        std::string value = ReadUtf8Text(label, length);
        return ImGui::Selectable(value.c_str(), selected != 0) ? 1 : 0;
    }

    // 绘制布尔输入框。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiCheckbox(const uint8* label, int32 length, uint8* value)
    {
        if (!value) return 0;

        bool boolValue = *value != 0;
        std::string text = ReadUtf8Text(label, length);
        bool changed = ImGui::Checkbox(text.c_str(), &boolValue);
        *value = boolValue ? 1 : 0;
        return changed ? 1 : 0;
    }

    // 绘制整数输入框。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiInputInt(const uint8* label, int32 length, int32* value)
    {
        if (!value) return 0;

        std::string text = ReadUtf8Text(label, length);
        return ImGui::InputInt(text.c_str(), value) ? 1 : 0;
    }

    // 绘制浮点输入框。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiInputFloat(const uint8* label, int32 length, float32* value)
    {
        if (!value) return 0;

        std::string text = ReadUtf8Text(label, length);
        return ImGui::InputFloat(text.c_str(), value) ? 1 : 0;
    }

    // 绘制三维向量输入框。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiInputVector3(const uint8* label, int32 length, vector3* value)
    {
        if (!value) return 0;

        std::string text = ReadUtf8Text(label, length);
        float values[3] = { value->x, value->y, value->z };
        bool changed = ImGui::InputFloat3(text.c_str(), values);
        if (changed)
        {
            value->x = values[0];
            value->y = values[1];
            value->z = values[2];
        }

        return changed ? 1 : 0;
    }

    // 绘制字符串输入框。
    int32 ORBEDEN_NATIVE_CALL RuntimeGuiInputText(const uint8* label, int32 length, uint8* buffer, int32 bufferSize)
    {
        if (!buffer || bufferSize <= 0) return -1;

        std::string text = ReadUtf8Text(label, length);
        buffer[bufferSize - 1] = 0;
        bool changed = ImGui::InputText(text.c_str(), reinterpret_cast<char*>(buffer), static_cast<size_t>(bufferSize));
        return changed ? static_cast<int32>(std::strlen(reinterpret_cast<const char*>(buffer))) : -1;
    }

    //绘制分隔线。
    void ORBEDEN_NATIVE_CALL RuntimeGuiSeparator()
    {
        ImGui::Separator();
    }

    //让下一个控件与前一个控件同行。
    void ORBEDEN_NATIVE_CALL RuntimeGuiSameLine()
    {
        ImGui::SameLine();
    }

    //开始一个可滚动表格。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiBeginTable(const uint8* id, int32 length, int32 columns)
    {
        if (columns <= 0) return 0;

        std::string value = ReadUtf8Text(id, length);
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_SizingStretchProp
            | ImGuiTableFlags_ScrollY;
        return ImGui::BeginTable(value.empty() ? "##managed_table" : value.c_str(), columns, flags, ImVec2(0.0f, 0.0f)) ? 1 : 0;
    }

    //结束当前表格。
    void ORBEDEN_NATIVE_CALL RuntimeGuiEndTable()
    {
        ImGui::EndTable();
    }

    //配置一个表格列。
    void ORBEDEN_NATIVE_CALL RuntimeGuiTableSetupColumn(const uint8* label, int32 length, float32 width, uint8 fixedWidth)
    {
        std::string value = ReadUtf8Text(label, length);
        ImGuiTableColumnFlags flags = fixedWidth != 0 ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch;
        ImGui::TableSetupColumn(value.c_str(), flags, width);
    }

    //绘制表头。
    void ORBEDEN_NATIVE_CALL RuntimeGuiTableHeadersRow()
    {
        ImGui::TableHeadersRow();
    }

    //前进到下一表格行。
    void ORBEDEN_NATIVE_CALL RuntimeGuiTableNextRow()
    {
        ImGui::TableNextRow();
    }

    //切换当前表格列。
    void ORBEDEN_NATIVE_CALL RuntimeGuiTableSetColumnIndex(int32 column)
    {
        ImGui::TableSetColumnIndex(column);
    }

    //绘制支持跨列和双击的选择项。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiProjectSelectable(const uint8* label, int32 length, uint8 selected, uint8 spanAllColumns)
    {
        std::string value = ReadUtf8Text(label, length);
        ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
        if (spanAllColumns != 0) flags |= ImGuiSelectableFlags_SpanAllColumns;
        return ImGui::Selectable(value.c_str(), selected != 0, flags) ? 1 : 0;
    }

    //判断刚绘制的控件是否被左键双击。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiIsItemDoubleClicked()
    {
        return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ? 1 : 0;
    }

    //开始刚绘制控件的右键菜单。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiBeginPopupContextItem(const uint8* id, int32 length)
    {
        std::string value = ReadUtf8Text(id, length);
        return ImGui::BeginPopupContextItem(value.empty() ? nullptr : value.c_str()) ? 1 : 0;
    }

    //开始当前窗口空白区域的右键菜单。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiBeginPopupContextWindow(const uint8* id, int32 length)
    {
        std::string value = ReadUtf8Text(id, length);
        constexpr ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems;
        return ImGui::BeginPopupContextWindow(value.empty() ? nullptr : value.c_str(), flags) ? 1 : 0;
    }

    //结束当前右键菜单。
    void ORBEDEN_NATIVE_CALL RuntimeGuiEndPopup()
    {
        ImGui::EndPopup();
    }

    //绘制右键菜单项。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiMenuItem(const uint8* label, int32 length, uint8 enabled)
    {
        std::string value = ReadUtf8Text(label, length);
        return ImGui::MenuItem(value.c_str(), nullptr, false, enabled != 0) ? 1 : 0;
    }

    //写入系统剪贴板文本。
    void ORBEDEN_NATIVE_CALL RuntimeGuiSetClipboardText(const uint8* text, int32 length)
    {
        std::string value = ReadUtf8Text(text, length);
        ImGui::SetClipboardText(value.c_str());
    }

    //开始禁用控件区域。
    void ORBEDEN_NATIVE_CALL RuntimeGuiBeginDisabled(uint8 disabled)
    {
        ImGui::BeginDisabled(disabled != 0);
    }

    //结束禁用控件区域。
    void ORBEDEN_NATIVE_CALL RuntimeGuiEndDisabled()
    {
        ImGui::EndDisabled();
    }
}

RuntimeGuiApi RuntimeGuiBridge::GetApi()
{
    RuntimeGuiApi api;
    api.Label = reinterpret_cast<void*>(&RuntimeGuiLabel);
    api.Button = reinterpret_cast<void*>(&RuntimeGuiButton);
    api.BeginPanel = reinterpret_cast<void*>(&RuntimeGuiBeginPanel);
    api.EndPanel = reinterpret_cast<void*>(&RuntimeGuiEndPanel);
    api.Checkbox = reinterpret_cast<void*>(&RuntimeGuiCheckbox);
    api.InputInt = reinterpret_cast<void*>(&RuntimeGuiInputInt);
    api.InputFloat = reinterpret_cast<void*>(&RuntimeGuiInputFloat);
    api.InputVector3 = reinterpret_cast<void*>(&RuntimeGuiInputVector3);
    api.InputText = reinterpret_cast<void*>(&RuntimeGuiInputText);
    api.BeginComponentBlock = reinterpret_cast<void*>(&RuntimeGuiBeginComponentBlock);
    api.EndComponentBlock = reinterpret_cast<void*>(&RuntimeGuiEndComponentBlock);
    return api;
}

RuntimeGuiExtensionApi RuntimeGuiBridge::GetExtensionApi()
{
    RuntimeGuiExtensionApi api;
    api.BeginCollapsibleComponentBlock = reinterpret_cast<void*>(&RuntimeGuiBeginCollapsibleComponentBlock);
    api.BeginCombo = reinterpret_cast<void*>(&RuntimeGuiBeginCombo);
    api.EndCombo = reinterpret_cast<void*>(&RuntimeGuiEndCombo);
    api.Selectable = reinterpret_cast<void*>(&RuntimeGuiSelectable);
    return api;
}

RuntimeGuiProjectApi RuntimeGuiBridge::GetProjectApi()
{
    RuntimeGuiProjectApi api;
    api.Separator = reinterpret_cast<void*>(&RuntimeGuiSeparator);
    api.SameLine = reinterpret_cast<void*>(&RuntimeGuiSameLine);
    api.BeginTable = reinterpret_cast<void*>(&RuntimeGuiBeginTable);
    api.EndTable = reinterpret_cast<void*>(&RuntimeGuiEndTable);
    api.TableSetupColumn = reinterpret_cast<void*>(&RuntimeGuiTableSetupColumn);
    api.TableHeadersRow = reinterpret_cast<void*>(&RuntimeGuiTableHeadersRow);
    api.TableNextRow = reinterpret_cast<void*>(&RuntimeGuiTableNextRow);
    api.TableSetColumnIndex = reinterpret_cast<void*>(&RuntimeGuiTableSetColumnIndex);
    api.Selectable = reinterpret_cast<void*>(&RuntimeGuiProjectSelectable);
    api.IsItemDoubleClicked = reinterpret_cast<void*>(&RuntimeGuiIsItemDoubleClicked);
    api.BeginPopupContextItem = reinterpret_cast<void*>(&RuntimeGuiBeginPopupContextItem);
    api.BeginPopupContextWindow = reinterpret_cast<void*>(&RuntimeGuiBeginPopupContextWindow);
    api.EndPopup = reinterpret_cast<void*>(&RuntimeGuiEndPopup);
    api.MenuItem = reinterpret_cast<void*>(&RuntimeGuiMenuItem);
    api.SetClipboardText = reinterpret_cast<void*>(&RuntimeGuiSetClipboardText);
    api.BeginDisabled = reinterpret_cast<void*>(&RuntimeGuiBeginDisabled);
    api.EndDisabled = reinterpret_cast<void*>(&RuntimeGuiEndDisabled);
    return api;
}
