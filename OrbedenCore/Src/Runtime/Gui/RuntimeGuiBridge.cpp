#include "Runtime/Gui/RuntimeGuiBridge.h"

#include <coreclr_delegates.h>
#include <imgui.h>

#include <string>

namespace
{
    // 从 C# 传入的 UTF-8 字节创建临时字符串。
    std::string MakeText(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(length));
    }

    // 绘制文本标签。
    void CORECLR_DELEGATE_CALLTYPE RuntimeGuiLabel(const uint8* text, int32 length)
    {
        std::string value = MakeText(text, length);
        ImGui::TextUnformatted(value.c_str());
    }

    // 绘制按钮并返回是否点击。
    uint8 CORECLR_DELEGATE_CALLTYPE RuntimeGuiButton(const uint8* text, int32 length)
    {
        std::string value = MakeText(text, length);
        return ImGui::Button(value.c_str()) ? 1 : 0;
    }

    // 开始一个浮动面板。
    uint8 CORECLR_DELEGATE_CALLTYPE RuntimeGuiBeginPanel(const uint8* title, int32 length)
    {
        std::string value = MakeText(title, length);
        return ImGui::Begin(value.empty() ? "Managed Panel" : value.c_str()) ? 1 : 0;
    }

    // 结束一个浮动面板。
    void CORECLR_DELEGATE_CALLTYPE RuntimeGuiEndPanel()
    {
        ImGui::End();
    }
}

RuntimeGuiApi RuntimeGuiBridge::GetApi()
{
    RuntimeGuiApi api;
    api.Label = reinterpret_cast<void*>(&RuntimeGuiLabel);
    api.Button = reinterpret_cast<void*>(&RuntimeGuiButton);
    api.BeginPanel = reinterpret_cast<void*>(&RuntimeGuiBeginPanel);
    api.EndPanel = reinterpret_cast<void*>(&RuntimeGuiEndPanel);
    return api;
}
