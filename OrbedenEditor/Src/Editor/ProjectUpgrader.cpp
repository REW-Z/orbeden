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

    //内容根的初始子目录。新建与升级共用同一套默认结构。
    const char* const ContentFolders[] =
    {
        "Meshes", "Materials", "Textures", "Shaders", "Scenes", "Scripts",
    };

    //旧布局按类别分目录且场景目录叫 World/，升级时按目录段改名到新默认结构。
    std::string MapLegacyContentFolder(const std::string& name)
    {
        if (name == "Mesh") return "Meshes";
        if (name == "Material") return "Materials";
        if (name == "Texture") return "Textures";
        if (name == "Shader") return "Shaders";
        if (name == "World") return "Scenes";
        return name;
    }

    //把旧路径映射成新的内容根相对路径：吃掉 Resource/ 这一层，其余按目录段改名。
    //启动场景等引用要跟着资源一起改，所以复用同一套规则。
    std::string MapLegacyContentPath(const std::string& path)
    {
        std::string result;
        std::size_t position = 0;
        while (position <= path.size())
        {
            std::size_t separator = path.find('/', position);
            bool isLast = separator == std::string::npos;
            std::string segment = path.substr(position, isLast ? std::string::npos : separator - position);

            if (segment != "Resource")
            {
                if (!result.empty()) result += '/';
                result += MapLegacyContentFolder(segment);
            }

            if (isLast) break;
            position = separator + 1;
        }

        return result;
    }

    //引擎拥有的脚手架文件：升级时一律删除后由模板重铺，不保留项目内的副本。
    //GameModule.cpp 与工程文件按名字识别，这样它们混在旧内容目录里时也不会被当成内容搬走。
    bool IsEngineScaffoldFile(const std::filesystem::path& path)
    {
        std::string name = Utf8Path::ToUtf8(path.filename());
        if (name == "Directory.Build.props" || name == "GameModule.cpp"
            || name == "OrbedenAotExports.cs" || name == ".gitignore") return true;

        std::string extension = Utf8Path::ToUtf8(path.extension());
        return extension == ".csproj" || extension == ".vcxproj" || extension == ".filters" || extension == ".oeproj";
    }

    //旧布局里位于项目根的引擎目录，升级时整体删除后重铺。
    bool IsEngineScaffoldFolder(const std::string& name)
    {
        return name == "Script" || name == "Managed" || name == "Aot" || name == "Build" || name == "Lib";
    }

    //把一个目录的内容搬进内容根，跳过引擎脚手架文件。
    bool MoveIntoContentRoot(const std::filesystem::path& source,
        const std::filesystem::path& contentRoot,
        const std::string& targetName,
        std::string& outError,
        int32& outMovedCount)
    {
        if (!std::filesystem::exists(source)) return true;

        std::filesystem::path target = contentRoot / Utf8Path::FromUtf8(targetName);
        std::error_code error;
        if (std::filesystem::exists(target, error))
        {
            outError = "Cannot move " + ToCleanPath(source) + ": " + ToCleanPath(target) + " already exists.";
            Log::Error(outError.c_str());
            return false;
        }

        std::filesystem::create_directories(target.parent_path());
        std::filesystem::rename(source, target, error);
        if (error)
        {
            outError = "Move failed for " + ToCleanPath(source) + ": " + error.message();
            Log::Error(outError.c_str());
            return false;
        }

        outMovedCount++;
        return true;
    }

    //把项目根的内容搬进内容根；Resource/ 这一层展开，其余目录整体改名搬入。
    bool RelocateContent(const std::filesystem::path& projectRoot,
        const std::filesystem::path& contentRoot,
        std::string& outError,
        int32& outMovedCount)
    {
        std::error_code error;
        List<std::filesystem::path> entries;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(projectRoot, error))
        {
            if (error)
            {
                outError = "Project root scan failed: " + error.message();
                return false;
            }

            entries.push_back(entry.path());
        }

        for (const std::filesystem::path& entry : entries)
        {
            std::string name = Utf8Path::ToUtf8(entry.filename());

            //内容根、项目文件、版本库与编辑器缓存都不动。
            if (name == ProjectLayout::ContentFolder) continue;
            if (name.empty() || name[0] == '.') continue;
            if (IsEngineScaffoldFile(entry)) continue;
            if (std::filesystem::is_directory(entry) && IsEngineScaffoldFolder(name)) continue;

            //Resource/ 只是旧布局的一层包装，把它下面每一段按类别展开到内容根。
            if (name == "Resource" && std::filesystem::is_directory(entry))
            {
                for (const std::filesystem::directory_entry& child : std::filesystem::directory_iterator(entry, error))
                {
                    if (error) break;
                    std::string childName = MapLegacyContentFolder(Utf8Path::ToUtf8(child.path().filename()));
                    if (!MoveIntoContentRoot(child.path(), contentRoot, childName, outError, outMovedCount)) return false;
                }
                if (error)
                {
                    outError = "Resource migration failed: " + error.message();
                    return false;
                }

                std::filesystem::remove_all(entry, error);
                continue;
            }

            if (!MoveIntoContentRoot(entry, contentRoot, MapLegacyContentFolder(name), outError, outMovedCount)) return false;
        }

        return true;
    }

    //清空内容根之外残余的引擎文件：脚手架已重铺，留着只会造成版本不一致。
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

    void CreateContentFolders(const std::filesystem::path& contentRoot)
    {
        for (const char* folder : ContentFolders)
        {
            std::error_code error;
            std::filesystem::create_directories(contentRoot / folder, error);
        }
    }
}

bool ProjectUpgrader::UpgradeProject(const UpgradeRequest& request, std::string& outError)
{
    outError.clear();

    std::filesystem::path projectRoot = Utf8Path::FromUtf8(request.projectRoot);
    std::filesystem::path contentRoot = projectRoot / ProjectLayout::ContentFolder;
    std::filesystem::create_directories(contentRoot);

    //1. 内容归位：旧布局把内容和工程混在项目根，先把内容收进内容根。
    int32 movedCount = 0;
    if (!RelocateContent(projectRoot, contentRoot, outError, movedCount)) return false;
    Log::Info(("Project upgrade moved " + std::to_string(movedCount) + " entries into " + ProjectLayout::ContentFolder + "/").c_str());

    //2. 清空内容根之外：脚手架与产物全部由下一步重铺。
    ClearOutsideContentRoot(projectRoot, Utf8Path::FromUtf8(request.projectFilePath));

    //3. 重铺脚手架（工程文件铺到项目根，示例铺到内容根的 Examples/）。
    if (!NewProjectTemplate::GenerateProjectFiles(ToCleanPath(projectRoot), request.projectName, request.templateRoot, outError)) return false;
    CreateContentFolders(contentRoot);

    //4. 恢复启动场景：映射后仍然存在才恢复，否则保留模板默认（示例场景）。
    std::string mappedStartupWorld = MapLegacyContentPath(request.startupWorld);
    bool restoreStartupWorld = !mappedStartupWorld.empty()
        && std::filesystem::exists(contentRoot / Utf8Path::FromUtf8(mappedStartupWorld));

    //5. SDK 产物：Core C# 运行库、绑定目标、SDK 路径。
    std::string scriptProject = ToCleanPath(projectRoot / (request.projectName + ".csproj"));
    if (!NewProjectGenerator::SyncRuntimeCSharpDll(scriptProject, request.runtimeDllPath, outError)) return false;

    //6. 根属性最后写：它们是"升级完成"的提交点，前面任何一步失败都不该走到这里。
    //不写 name：项目名由 .oeproj 文件基名决定，写进去反而会和文件改名脱节。
    List<std::pair<std::string, std::string>> attributes =
    {
        std::make_pair(std::string("version"), std::to_string(OrbedenProjectVersion)),
    };
    if (restoreStartupWorld)
    {
        attributes.push_back(std::make_pair(std::string("startupWorld"), mappedStartupWorld));
    }

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
