#include "Runtime/Gui/RuntimeGuiBridge.h"

#include "Runtime/Native/NativeCall.h"
#include "Runtime/EngineTypes.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

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
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiTableSelectable(const uint8* label, int32 length, uint8 selected, uint8 spanAllColumns)
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

    //获取当前绘制窗口内容区域的原点。
    ImVec2 GetDrawOrigin()
    {
        ImVec2 position = ImGui::GetWindowPos();
        ImVec2 padding = ImGui::GetStyle().WindowPadding;
        return ImVec2(position.x + padding.x, position.y + padding.y);
    }

    //平移一个 ImVec2，避免依赖 ImGui 的可选运算符重载。
    ImVec2 Offset(const ImVec2& value, float32 x, float32 y)
    {
        return ImVec2(value.x + x, value.y + y);
    }

    //开始一个固定位置、无边框、不响应输入的绘制窗口。
    uint8 ORBEDEN_NATIVE_CALL RuntimeGuiBeginFixedWindow(const uint8* title, int32 length, float32 x, float32 y, float32 width, float32 height)
    {
        std::string value = ReadUtf8Text(title, length);
        ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs;
        bool open = ImGui::Begin(value.empty() ? "##FixedWindow" : value.c_str(), nullptr, flags);
        return open ? 1 : 0;
    }

    //结束固定绘制窗口。
    void ORBEDEN_NATIVE_CALL RuntimeGuiEndFixedWindow()
    {
        ImGui::End();
    }

    //获取主视口工作区尺寸。
    void ORBEDEN_NATIVE_CALL RuntimeGuiGetViewportSize(float32* width, float32* height)
    {
        if (!width || !height) return;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
        {
            *width = 0.0f;
            *height = 0.0f;
            return;
        }

        *width = viewport->WorkSize.x;
        *height = viewport->WorkSize.y;
    }

    //绘制线段。
    void ORBEDEN_NATIVE_CALL RuntimeGuiDrawLine(float32 x0, float32 y0, float32 x1, float32 y1, uint32 color, float32 thickness)
    {
        ImVec2 origin = GetDrawOrigin();
        ImGui::GetWindowDrawList()->AddLine(Offset(origin, x0, y0), Offset(origin, x1, y1), color, thickness);
    }

    //绘制折线。
    void ORBEDEN_NATIVE_CALL RuntimeGuiDrawPolyline(const vector2* points, int32 count, uint32 color, float32 thickness, uint8 closed)
    {
        if (!points || count < 2) return;

        ImVec2 origin = GetDrawOrigin();
        std::vector<ImVec2> converted;
        converted.reserve(static_cast<size_t>(count));
        for (int32 index = 0; index < count; index++)
        {
            converted.push_back(Offset(origin, points[index].x, points[index].y));
        }

        ImDrawFlags flags = closed != 0 ? ImDrawFlags_Closed : ImDrawFlags_None;
        ImGui::GetWindowDrawList()->AddPolyline(converted.data(), count, color, flags, thickness);
    }

    //绘制矩形边框。
    void ORBEDEN_NATIVE_CALL RuntimeGuiDrawRect(float32 minX, float32 minY, float32 maxX, float32 maxY, uint32 color, float32 thickness, float32 rounding)
    {
        ImVec2 origin = GetDrawOrigin();
        ImGui::GetWindowDrawList()->AddRect(Offset(origin, minX, minY), Offset(origin, maxX, maxY), color, rounding, 0, thickness);
    }

    //绘制实心矩形。
    void ORBEDEN_NATIVE_CALL RuntimeGuiDrawRectFilled(float32 minX, float32 minY, float32 maxX, float32 maxY, uint32 color, float32 rounding)
    {
        ImVec2 origin = GetDrawOrigin();
        ImGui::GetWindowDrawList()->AddRectFilled(Offset(origin, minX, minY), Offset(origin, maxX, maxY), color, rounding);
    }

    //绘制圆形边框。
    void ORBEDEN_NATIVE_CALL RuntimeGuiDrawCircle(float32 cx, float32 cy, float32 radius, uint32 color, float32 thickness, int32 segments)
    {
        ImVec2 origin = GetDrawOrigin();
        ImGui::GetWindowDrawList()->AddCircle(Offset(origin, cx, cy), radius, color, std::max(segments, 3), thickness);
    }

    //绘制实心圆。
    void ORBEDEN_NATIVE_CALL RuntimeGuiDrawCircleFilled(float32 cx, float32 cy, float32 radius, uint32 color, int32 segments)
    {
        ImVec2 origin = GetDrawOrigin();
        ImGui::GetWindowDrawList()->AddCircleFilled(Offset(origin, cx, cy), radius, color, std::max(segments, 3));
    }

    //绘制圆弧。
    void ORBEDEN_NATIVE_CALL RuntimeGuiDrawArc(float32 cx, float32 cy, float32 radius, float32 minAngle, float32 maxAngle, uint32 color, float32 thickness, int32 segments)
    {
        ImVec2 origin = GetDrawOrigin();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PathClear();
        drawList->PathArcTo(Offset(origin, cx, cy), radius, minAngle, maxAngle, std::max(segments, 3));
        drawList->PathStroke(color, ImDrawFlags_None, thickness);
    }

    //绘制实心三角形。
    void ORBEDEN_NATIVE_CALL RuntimeGuiDrawTriangleFilled(float32 x0, float32 y0, float32 x1, float32 y1, float32 x2, float32 y2, uint32 color)
    {
        ImVec2 origin = GetDrawOrigin();
        ImGui::GetWindowDrawList()->AddTriangleFilled(Offset(origin, x0, y0), Offset(origin, x1, y1), Offset(origin, x2, y2), color);
    }

    //绘制文本，字号按默认字体大小缩放。
    void ORBEDEN_NATIVE_CALL RuntimeGuiDrawText(const uint8* text, int32 length, float32 x, float32 y, uint32 color, float32 fontScale)
    {
        ImVec2 origin = GetDrawOrigin();
        std::string value = ReadUtf8Text(text, length);
        float32 fontSize = ImGui::GetFontSize() * std::max(fontScale, 0.01f);
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), fontSize, Offset(origin, x, y), color, value.c_str());
    }

    //获取文本绘制尺寸。
    void ORBEDEN_NATIVE_CALL RuntimeGuiGetTextSize(const uint8* text, int32 length, float32 fontScale, float32* width, float32* height)
    {
        if (!width || !height) return;

        std::string value = ReadUtf8Text(text, length);
        ImVec2 size = ImGui::CalcTextSize(value.c_str());
        float32 scale = std::max(fontScale, 0.01f);
        *width = size.x * scale;
        *height = size.y * scale;
    }

    //推入裁剪区域。
    void ORBEDEN_NATIVE_CALL RuntimeGuiPushClipRect(float32 minX, float32 minY, float32 maxX, float32 maxY, uint8 intersectWithCurrent)
    {
        ImVec2 origin = GetDrawOrigin();
        ImGui::GetWindowDrawList()->PushClipRect(Offset(origin, minX, minY), Offset(origin, maxX, maxY), intersectWithCurrent != 0);
    }

    //弹出裁剪区域。
    void ORBEDEN_NATIVE_CALL RuntimeGuiPopClipRect()
    {
        ImGui::GetWindowDrawList()->PopClipRect();
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

RuntimeGuiAdvancedApi RuntimeGuiBridge::GetAdvancedApi()
{
    RuntimeGuiAdvancedApi api;
    api.Separator = reinterpret_cast<void*>(&RuntimeGuiSeparator);
    api.SameLine = reinterpret_cast<void*>(&RuntimeGuiSameLine);
    api.BeginTable = reinterpret_cast<void*>(&RuntimeGuiBeginTable);
    api.EndTable = reinterpret_cast<void*>(&RuntimeGuiEndTable);
    api.TableSetupColumn = reinterpret_cast<void*>(&RuntimeGuiTableSetupColumn);
    api.TableHeadersRow = reinterpret_cast<void*>(&RuntimeGuiTableHeadersRow);
    api.TableNextRow = reinterpret_cast<void*>(&RuntimeGuiTableNextRow);
    api.TableSetColumnIndex = reinterpret_cast<void*>(&RuntimeGuiTableSetColumnIndex);
    api.Selectable = reinterpret_cast<void*>(&RuntimeGuiTableSelectable);
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

RuntimeGuiDrawApi RuntimeGuiBridge::GetDrawApi()
{
    RuntimeGuiDrawApi api;
    api.BeginFixedWindow = reinterpret_cast<void*>(&RuntimeGuiBeginFixedWindow);
    api.EndFixedWindow = reinterpret_cast<void*>(&RuntimeGuiEndFixedWindow);
    api.GetViewportSize = reinterpret_cast<void*>(&RuntimeGuiGetViewportSize);
    api.Line = reinterpret_cast<void*>(&RuntimeGuiDrawLine);
    api.Polyline = reinterpret_cast<void*>(&RuntimeGuiDrawPolyline);
    api.Rect = reinterpret_cast<void*>(&RuntimeGuiDrawRect);
    api.RectFilled = reinterpret_cast<void*>(&RuntimeGuiDrawRectFilled);
    api.Circle = reinterpret_cast<void*>(&RuntimeGuiDrawCircle);
    api.CircleFilled = reinterpret_cast<void*>(&RuntimeGuiDrawCircleFilled);
    api.Arc = reinterpret_cast<void*>(&RuntimeGuiDrawArc);
    api.TriangleFilled = reinterpret_cast<void*>(&RuntimeGuiDrawTriangleFilled);
    api.Text = reinterpret_cast<void*>(&RuntimeGuiDrawText);
    api.GetTextSize = reinterpret_cast<void*>(&RuntimeGuiGetTextSize);
    api.PushClipRect = reinterpret_cast<void*>(&RuntimeGuiPushClipRect);
    api.PopClipRect = reinterpret_cast<void*>(&RuntimeGuiPopClipRect);
    return api;
}
