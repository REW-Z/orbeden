#include "Editor/Panels/BuildGamePanel.h"

#include "Editor/EditorSystem.h"
#include "Editor/Panels/EditorPanelRegistry.h"

#include <imgui.h>
#include <string>

BuildGamePanel::BuildGamePanel(EditorSystem& owner)
    : editor(owner)
{
    info.id = "build_game";
    info.title = "Build Game";
    info.defaultVisible = true;
    info.defaultSize = { 360.0f, 180.0f };
    info.defaultDock = PanelDockPlacement::Floating;
    info.order = 110;
}

//获取面板信息
const EditorPanelInfo& BuildGamePanel::GetPanelInfo() const
{
    return info;
}

//绘制面板内容
void BuildGamePanel::DrawPanel()
{
    if (editor.HasProject())
    {
        ImGui::Text("Name: %s", editor.GetProjectName().c_str());
        ImGui::TextWrapped("Root: %s", editor.GetProjectRoot().c_str());
        ImGui::TextWrapped("World: %s", editor.GetStartupWorldPath().c_str());
        ImGui::TextWrapped("Script: %s", editor.GetProjectScriptRootPath().c_str());
        ImGui::TextWrapped("Managed: %s", editor.GetProjectManagedRootPath().c_str());
        std::string nativeRoot = editor.GetProjectNativeRootPath();
        ImGui::TextWrapped("Native: %s", nativeRoot.empty() ? "<disabled>" : nativeRoot.c_str());
    }
    else
    {
        ImGui::TextUnformatted("No project loaded.");
    }

    ImGui::Separator();

    if (!editor.HasProject())
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Build Game C#"))
    {
        editor.RequestBuildScripts();
    }

    ImGui::SameLine();
    if (ImGui::Button("Build Game C++"))
    {
        editor.RequestBuildNative();
    }

    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Target Platform", editor.GetSelectedPlayerTargetPlatformName()))
    {
        int32 selectedIndex = editor.GetSelectedPlayerTargetPlatformIndex();
        for (int32 index = 0; index < editor.GetPlayerTargetPlatformCount(); ++index)
        {
            bool selected = index == selectedIndex;
            if (ImGui::Selectable(editor.GetPlayerTargetPlatformName(index), selected))
            {
                editor.SetSelectedPlayerTargetPlatformIndex(index);
            }

            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Build Player"))
    {
        editor.RequestBuildPlayer();
    }

    if (!editor.HasProject())
    {
        ImGui::EndDisabled();
    }

    const std::string& status = editor.GetProjectStatusText();
    if (!status.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", status.c_str());
    }
}

ORBEDEN_REGISTER_EDITOR_PANEL(BuildGamePanel)
