#include "Editor/PanelManager.h"

#include <imgui.h>

#include <algorithm>

namespace
{
    constexpr float32 MinPanelWidth = 120.0f;
    constexpr float32 MinPanelHeight = 80.0f;

    //转换为 ImGui 二维向量。
    ImVec2 ToImVec2(const vector2& value)
    {
        return ImVec2(value.x, value.y);
    }

    //转换为引擎二维向量。
    vector2 ToVector2(const ImVec2& value)
    {
        return vector2 { value.x, value.y };
    }
}

//注册一个面板实例
void PanelManager::RegisterPanel(const PanelInfo& info, IEditorPanel* panel)
{
    if (!panel || !info.id || info.id[0] == '\0') return;

    PanelEntry* oldEntry = FindPanel(info.id);
    if (oldEntry)
    {
        oldEntry->info = info;
        oldEntry->id = info.id;
        oldEntry->title = info.title ? info.title : info.id;
        oldEntry->panel = panel;
        ApplyVisibility(*oldEntry, info.defaultVisible);
        return;
    }

    PanelEntry entry;
    entry.info = info;
    entry.id = info.id;
    entry.title = info.title ? info.title : info.id;
    entry.panel = panel;
    entry.visible = info.defaultVisible;
    entry.size = info.defaultSize;
    panels.push_back(entry);
}

//绘制 Views 菜单内容
void PanelManager::DrawViewsMenu()
{
    if (panels.empty())
    {
        ImGui::TextUnformatted("No panels.");
        return;
    }

    for (PanelEntry& entry : panels)
    {
        bool visible = entry.visible;
        if (ImGui::Checkbox(entry.title.c_str(), &visible))
        {
            ApplyVisibility(entry, visible);
        }
    }
}

//绘制所有可见面板
void PanelManager::DrawPanels()
{
    for (PanelEntry& entry : panels)
    {
        if (!entry.visible || !entry.panel) continue;

        ClampPanel(entry);
        if (entry.applySize && entry.hasSize)
        {
            ImGui::SetNextWindowSize(ToImVec2(entry.size), ImGuiCond_Always);
        }
        else
        {
            ImGui::SetNextWindowSize(ToImVec2(entry.info.defaultSize), ImGuiCond_FirstUseEver);
        }
        if (entry.applyPosition && entry.hasPosition)
        {
            ImGui::SetNextWindowPos(ToImVec2(entry.position), ImGuiCond_Always);
        }

        bool visible = entry.visible;
        bool open = ImGui::Begin(entry.title.c_str(), &visible, ImGuiWindowFlags_NoSavedSettings);
        if (open)
        {
            entry.panel->DrawPanel();
        }

        entry.hasPosition = true;
        entry.hasSize = true;
        entry.position = ToVector2(ImGui::GetWindowPos());
        entry.size = ToVector2(ImGui::GetWindowSize());
        if (ClampPanelRect(entry.position, entry.size))
        {
            ImGui::SetWindowPos(ToImVec2(entry.position), ImGuiCond_Always);
            ImGui::SetWindowSize(ToImVec2(entry.size), ImGuiCond_Always);
        }
        entry.applyPosition = false;
        entry.applySize = false;
        ImGui::End();

        if (visible != entry.visible)
        {
            ApplyVisibility(entry, visible);
        }
    }
}

//应用项目中保存的面板布局
void PanelManager::ApplyLayout(const EditorLayoutState& layout)
{
    for (const EditorPanelState& state : layout.panels)
    {
        PanelEntry* entry = FindPanel(state.id.c_str());
        if (!entry) continue;

        ApplyVisibility(*entry, state.visible);
        if (state.hasPosition)
        {
            entry->hasPosition = true;
            entry->position = state.position;
            entry->applyPosition = true;
        }
        if (state.hasSize && state.size.x > 0.0f && state.size.y > 0.0f)
        {
            entry->hasSize = true;
            entry->size = state.size;
            entry->applySize = true;
        }
    }
}

//写出当前面板布局
void PanelManager::WriteLayout(EditorLayoutState& layout) const
{
    layout.panels.clear();
    for (const PanelEntry& entry : panels)
    {
        EditorPanelState state;
        state.id = entry.id;
        state.visible = entry.visible;
        state.hasPosition = entry.hasPosition;
        state.hasSize = entry.hasSize;
        state.position = entry.position;
        state.size = entry.hasSize ? entry.size : entry.info.defaultSize;
        layout.panels.push_back(state);
    }
}

//判断面板是否可见
bool PanelManager::IsPanelVisible(const char* id) const
{
    const PanelEntry* entry = FindPanel(id);
    return entry && entry->visible;
}

//设置面板可见状态
void PanelManager::SetPanelVisible(const char* id, bool visible)
{
    PanelEntry* entry = FindPanel(id);
    if (!entry) return;

    ApplyVisibility(*entry, visible);
}

PanelManager::PanelEntry* PanelManager::FindPanel(const char* id)
{
    if (!id) return nullptr;

    for (PanelEntry& entry : panels)
    {
        if (entry.id == id) return &entry;
    }

    return nullptr;
}

const PanelManager::PanelEntry* PanelManager::FindPanel(const char* id) const
{
    return const_cast<PanelManager*>(this)->FindPanel(id);
}

//应用面板可见性。
void PanelManager::ApplyVisibility(PanelEntry& entry, bool visible)
{
    if (entry.visible == visible) return;

    entry.visible = visible;
    if (!entry.panel) return;

    if (entry.visible)
    {
        entry.panel->OnPanelShown();
    }
    else
    {
        entry.panel->OnPanelHidden();
    }
}

//限制面板停留在主窗口内。
void PanelManager::ClampPanel(PanelEntry& entry) const
{
    if (!entry.hasPosition && !entry.hasSize) return;

    vector2 size = entry.hasSize ? entry.size : entry.info.defaultSize;
    vector2 position = entry.position;
    bool clamped = ClampPanelRect(position, size);
    if (!clamped) return;

    if (entry.hasPosition)
    {
        entry.position = position;
    }
    if (entry.hasSize)
    {
        entry.size = size;
    }
}

bool PanelManager::ClampPanelRect(vector2& position, vector2& size) const
{
    if (!ImGui::GetCurrentContext() || ImGui::GetFrameCount() <= 0) return false;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return false;

    vector2 oldPosition = position;
    vector2 oldSize = size;
    float32 maxWidth = std::max(MinPanelWidth, viewport->WorkSize.x);
    float32 maxHeight = std::max(MinPanelHeight, viewport->WorkSize.y);
    size.x = std::clamp(size.x, MinPanelWidth, maxWidth);
    size.y = std::clamp(size.y, MinPanelHeight, maxHeight);

    float32 minX = viewport->WorkPos.x;
    float32 minY = viewport->WorkPos.y;
    float32 maxX = viewport->WorkPos.x + std::max(0.0f, viewport->WorkSize.x - size.x);
    float32 maxY = viewport->WorkPos.y + std::max(0.0f, viewport->WorkSize.y - size.y);
    position.x = std::clamp(position.x, minX, maxX);
    position.y = std::clamp(position.y, minY, maxY);

    return position.x != oldPosition.x
        || position.y != oldPosition.y
        || size.x != oldSize.x
        || size.y != oldSize.y;
}
