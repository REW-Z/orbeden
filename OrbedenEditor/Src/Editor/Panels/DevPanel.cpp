#include "Editor/Panels/DevPanel.h"

#include "Editor/EditorSystem.h"
#include "Editor/NewProjectTemplate.h"
#include "Editor/Panels/EditorPanelRegistry.h"
#include "FileSystem/Utf8Path.h"

#include <filesystem>
#include <imgui.h>

namespace
{
    constexpr const char* ExamplesFolder = "Examples";

    std::string JoinPath(const std::string& directory, const std::string& child)
    {
        return Utf8Path::ToUtf8(Utf8Path::FromUtf8(directory) / Utf8Path::FromUtf8(child));
    }

    bool IsDirectory(const std::string& path)
    {
        return !path.empty() && std::filesystem::is_directory(Utf8Path::FromUtf8(path));
    }
}

DevPanel::DevPanel(EditorSystem& owner)
    : editor(owner)
{
    info.id = "dev";
    info.title = "Dev";
    info.defaultVisible = false;
    info.defaultSize = { 520.0f, 280.0f };
    info.defaultDock = PanelDockPlacement::Floating;
    info.order = 120;
}

//获取面板信息
const EditorPanelInfo& DevPanel::GetPanelInfo() const
{
    return info;
}

std::string DevPanel::GetProjectExamplesPath() const
{
    if (!editor.HasProject()) return std::string();
    return JoinPath(editor.GetProjectContentRootPath(), ExamplesFolder);
}

std::string DevPanel::GetTemplateExamplesPath() const
{
    std::string templateRoot = editor.GetSourceTemplateRoot();
    if (templateRoot.empty()) return std::string();
    return JoinPath(templateRoot, ExamplesFolder);
}

//绘制面板内容
void DevPanel::DrawPanel()
{
    if (!editor.HasProject())
    {
        ImGui::TextUnformatted("No project loaded.");
        return;
    }

    const std::string projectExamples = GetProjectExamplesPath();
    const std::string templateExamples = GetTemplateExamplesPath();

    ImGui::TextWrapped("Project examples: %s", projectExamples.c_str());
    ImGui::TextWrapped("Repository root: %s", editor.GetRepositoryRoot().c_str());
    ImGui::TextWrapped("Template examples: %s",
        templateExamples.empty() ? "(not found)" : templateExamples.c_str());

    ImGui::Separator();

    //置灰原因集中判断，按钮下面直接说明，免得点了没反应还要去翻日志。
    std::string blocked;
    if (editor.IsPlaying())
    {
        blocked = "Stop Play-In-Editor first: the running game may still hold these files.";
    }
    else if (templateExamples.empty())
    {
        blocked = "The source template was not found. Write-back needs a checkout that contains OrbedenEditor/Templates.";
    }
    else if (!IsDirectory(projectExamples))
    {
        blocked = "This project has no Content/Examples directory.";
    }

    const bool enabled = blocked.empty();
    if (!enabled) ImGui::BeginDisabled();

    if (ImGui::Button("Write Back to Template (project -> template)"))
    {
        WriteBackExamples();
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset from Template (template -> project)"))
    {
        ResetExamplesFromTemplate();
    }

    if (!enabled)
    {
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", blocked.c_str());
    }

    ImGui::Separator();
    if (report.empty())
    {
        ImGui::TextUnformatted("No operation yet.");
    }
    else
    {
        ImGui::TextWrapped("%s", report.c_str());
    }
}

//把项目里迭代的示例写回模板
void DevPanel::WriteBackExamples()
{
    report.clear();

    const std::string source = GetProjectExamplesPath();
    const std::string target = GetTemplateExamplesPath();

    if (!editor.SaveCurrentWorld())
    {
        report = "Write-back failed: " + editor.GetProjectStatusText();
        return;
    }

    NewProjectTemplate::MirrorReport mirrorReport;
    std::string error;
    if (!NewProjectTemplate::MirrorTree(source, target, mirrorReport, error))
    {
        report = "Write-back failed: " + error;
        return;
    }

    report = "Wrote back to template: " + std::to_string(mirrorReport.added) + " added, "
        + std::to_string(mirrorReport.updated) + " updated, "
        + std::to_string(mirrorReport.removed) + " removed.\n"
        + "Review with: git diff OrbedenEditor/Templates";
}

//用模板重铺项目里的示例
void DevPanel::ResetExamplesFromTemplate()
{
    report.clear();

    if (!editor.SaveCurrentWorld())
    {
        report = "Reset failed: " + editor.GetProjectStatusText();
        return;
    }

    NewProjectTemplate::MirrorReport mirrorReport;
    std::string error;
    bool mirrored = NewProjectTemplate::MirrorTree(GetTemplateExamplesPath(), GetProjectExamplesPath(), mirrorReport, error);
    bool reloaded = editor.ReloadProjectContent();
    if (!mirrored)
    {
        report = "Reset failed: " + error;
        if (!reloaded) report += "\nContent reload failed: " + editor.GetProjectStatusText();
        return;
    }

    report = "Restored examples from template: " + std::to_string(mirrorReport.added) + " added, "
        + std::to_string(mirrorReport.updated) + " updated, "
        + std::to_string(mirrorReport.removed) + " removed.";
    if (!reloaded) report += "\nContent reload failed: " + editor.GetProjectStatusText();
}

ORBEDEN_REGISTER_EDITOR_PANEL(DevPanel)
