#include "Editor/ProjectPanel.h"

#include "Editor/EditorSystem.h"

#include <imgui.h>
#include <string>

ProjectPanel::ProjectPanel(EditorSystem& owner)
    : editor(owner)
{
}

//获取面板稳定ID
const char* ProjectPanel::GetPanelId() const
{
    return "project";
}

//获取面板显示标题
const char* ProjectPanel::GetPanelTitle() const
{
    return "Project";
}

//绘制面板内容
void ProjectPanel::DrawPanel()
{
    if (editor.HasProject())
    {
        ImGui::Text("Name: %s", editor.GetProjectName().c_str());
        ImGui::TextWrapped("Root: %s", editor.GetProjectRoot().c_str());
        ImGui::TextWrapped("World: %s", editor.GetStartupWorldPath().c_str());
        ImGui::TextWrapped("Script: %s", editor.GetProjectScriptRootPath().c_str());
        ImGui::TextWrapped("Managed: %s", editor.GetProjectManagedRootPath().c_str());
    }
    else
    {
        ImGui::TextUnformatted("No project loaded.");
    }

    ImGui::Separator();

    if (ImGui::Button("Load Project..."))
    {
        editor.RequestOpenProjectDialog();
    }

    ImGui::SameLine();
    if (!editor.HasProject())
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Save World"))
    {
        editor.RequestSaveCurrentWorld();
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
