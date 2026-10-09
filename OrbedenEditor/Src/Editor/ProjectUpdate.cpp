#include "Editor/ProjectUpdate.h"

#include "Defines/Version.h"
#include "Editor/EditorProject.h"
#include "Editor/NewProjectGenerator.h"
#include "Editor/NewProjectTemplate.h"
#include "Editor/ProjectLayout.h"
#include "Runtime/Native/InteropText.h"
#include "Log/Log.h"

#include <filesystem>

namespace
{
    //项目脚手架在模板根下的子目录名，与 NewProjectTemplate 里的一致。
    constexpr const char* ProjectFolder = "Project";

    //统一成正斜杠的 UTF-8 路径，与仓库其它地方的项目路径写法一致
    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return InteropText::PathToUtf8(path.lexically_normal());
    }
}

//用当前模板覆盖项目内容根之外的脚手架，最后写入当前项目版本号。
//只覆盖模板里有的文件，不删除任何东西；Content/ 与 .oeproj 不参与覆盖。
bool ProjectUpdate::UpdateProject(const std::string& projectRoot,
    const std::string& projectName,
    const std::string& templateRoot,
    const std::string& runtimeDllPath,
    std::string& outError)
{
    outError.clear();
    if (projectRoot.empty() || projectName.empty())
    {
        outError = "Project root or name is empty.";
        return false;
    }

    std::filesystem::path root = InteropText::PathFromUtf8(templateRoot);
    if (!std::filesystem::is_directory(root / ProjectFolder))
    {
        outError = "Project template directory was not found. Rebuild OrbedenEditor.";
        return false;
    }

    std::filesystem::path project = InteropText::PathFromUtf8(projectRoot);
    if (!std::filesystem::is_directory(project))
    {
        outError = "Project directory was not found: " + projectRoot;
        return false;
    }

    //覆盖脚手架。跳过 .oeproj：模板里那份只有 startupWorld，覆盖会把项目的启动场景
    //与上次编辑的场景一起冲掉。Build/ 由 CopyTemplateTree 自己跳过。
    if (!NewProjectTemplate::CopyTemplateTree(ToCleanPath(root / ProjectFolder), projectRoot, projectName,
        outError, /*skipProjectFile=*/true))
    {
        return false;
    }

    //重置内置内容。Builtin/ 是引擎的默认着色器、材质与基础网格，与脚手架同属引擎地盘，
    //随引擎一起更新。这里用的是镜像语义：模板里没有的文件会被删掉，等同于 Dev 面板的 Reset Builtin。
    //Examples/ 不在此列——那是给作者改的示例内容。
    NewProjectTemplate::MirrorReport builtinReport;
    if (!NewProjectTemplate::MirrorTree(ToCleanPath(root / NewProjectTemplate::BuiltinFolderName),
        ToCleanPath(project / ProjectLayout::ContentFolder / NewProjectTemplate::BuiltinFolderName),
        builtinReport, outError))
    {
        return false;
    }
    if (builtinReport.added != 0 || builtinReport.updated != 0 || builtinReport.removed != 0)
    {
        Log::Info(("Builtin content updated: " + std::to_string(builtinReport.added) + " added, "
            + std::to_string(builtinReport.updated) + " updated, "
            + std::to_string(builtinReport.removed) + " removed.").c_str());
    }

    //刷新 Lib/ 下的 SDK 快照：Core C# 运行库、绑定目标转发与 SDK 路径。
    if (!NewProjectGenerator::SyncRuntimeCSharpDll(ToCleanPath(project / (projectName + ".csproj")),
        runtimeDllPath, outError))
    {
        return false;
    }

    //最后才写版本号：写成功代表一次完整更新，中途失败就不写，下次打开仍会提示。
    return EditorProject::WriteProjectVersion(ToCleanPath(project / (projectName + ".oeproj")),
        OrbedenProjectVersion, outError);
}
