#include "Editor/ProjectUpgrader.h"

#include "Defines/Version.h"
#include "Editor/EditorProject.h"
#include "Editor/NewProjectGenerator.h"
#include "Editor/NewProjectTemplate.h"
#include "Editor/ProjectLayout.h"
#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"

#include <filesystem>

namespace
{
    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return Utf8Path::ToUtf8(path.lexically_normal());
    }

    //清空内容根之外的一切：脚手架与产物全部由下一步重铺。
    //项目文件必须保住：它是升级失败时唯一还能认出这个项目的东西。
    void ClearOutsideContentRoot(const std::filesystem::path& projectRoot, const std::filesystem::path& projectFilePath)
    {
        std::error_code error;
        List<std::filesystem::path> entries;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(projectRoot, error))
        {
            if (error) return;
            entries.push_back(entry.path());
        }

        for (const std::filesystem::path& entry : entries)
        {
            std::string name = Utf8Path::ToUtf8(entry.filename());
            if (name == ProjectLayout::ContentFolder) continue;
            if (name.empty() || name[0] == '.') continue;
            if (entry == projectFilePath) continue;

            std::error_code removeError;
            std::filesystem::remove_all(entry, removeError);
            if (removeError)
            {
                Log::Warning(("Upgrade could not remove " + ToCleanPath(entry) + ": " + removeError.message()).c_str());
            }
        }
    }
}

bool ProjectUpgrader::UpgradeProject(const UpgradeRequest& request, std::string& outError)
{
    outError.clear();

    std::filesystem::path projectRoot = Utf8Path::FromUtf8(request.projectRoot);

    //1. 游戏资产只存在于内容根内，内容根之外全是引擎的地盘：整块删除，下一步按模板重铺。
    ClearOutsideContentRoot(projectRoot, Utf8Path::FromUtf8(request.projectFilePath));

    //2. 只重铺脚手架，保留原项目配置和 Content/ 内全部内容。
    if (!NewProjectTemplate::GenerateProjectFiles(ToCleanPath(projectRoot), request.projectName, request.templateRoot, outError, true)) return false;

    //3. SDK 产物：Core C# 运行库、绑定目标、SDK 路径。
    std::string scriptProject = ToCleanPath(projectRoot / (request.projectName + ".csproj"));
    if (!NewProjectGenerator::SyncRuntimeCSharpDll(scriptProject, request.runtimeDllPath, outError)) return false;

    //4. 根属性最后写：它们是"升级完成"的提交点，前面任何一步失败都不该走到这里。
    //不写 name：项目名由 .oeproj 文件基名决定，写进去反而会和文件改名脱节。
    //不写 startupWorld：Content/ 原样保留，启动场景相对内容根的位置没有变化。
    List<std::pair<std::string, std::string>> attributes =
    {
        std::make_pair(std::string("version"), std::to_string(OrbedenProjectVersion)),
    };

    if (!EditorProject::UpdateProjectRootAttributes(request.projectFilePath,
        attributes,
        { "name", "resourceRoot", "scriptRoot", "managedRoot", "nativeRoot" },
        outError))
    {
        return false;
    }

    Log::Info(("Project upgraded to version " + std::to_string(OrbedenProjectVersion) + ": " + request.projectRoot).c_str());
    return true;
}
