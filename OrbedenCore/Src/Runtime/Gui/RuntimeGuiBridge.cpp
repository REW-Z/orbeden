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
        std::string value = ReadUtf8Text(text, length);
        ImGui::TextWrapped("%s", value.c_str());
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
        ImGui::InputText(text.c_str(), reinterpret_cast<char*>(buffer), static_cast<size_t>(bufferSize));
        return static_cast<int32>(std::strlen(reinterpret_cast<const char*>(buffer)));
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
