#include "Editor/PanelManager.h"

#include <imgui.h>

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

        ImGui::SetNextWindowSize(ImVec2(entry.info.defaultSize.x, entry.info.defaultSize.y), ImGuiCond_FirstUseEver);

        bool visible = entry.visible;
        bool open = ImGui::Begin(entry.title.c_str(), &visible, ImGuiWindowFlags_NoSavedSettings);
        if (open)
        {
            entry.panel->DrawPanel();
        }
        ImGui::End();

        if (visible != entry.visible)
        {
            ApplyVisibility(entry, visible);
        }
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
