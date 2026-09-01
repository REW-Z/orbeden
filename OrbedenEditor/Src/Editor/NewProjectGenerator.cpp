#include "Editor/NewProjectGenerator.h"
#include "Editor/NewProjectTemplate.h"

#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return Utf8Path::ToUtf8(path.lexically_normal());
    }

    bool IsProjectNameChar(char ch)
    {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    }

    bool IsValidProjectName(const std::string& projectName)
    {
        if (projectName.empty()) return false;
        if (!std::isalpha(static_cast<unsigned char>(projectName[0])) && projectName[0] != '_') return false;

        for (char ch : projectName)
        {
            if (!IsProjectNameChar(ch)) return false;
        }

        return true;
    }

    bool WriteTextFile(const std::filesystem::path& path, const std::string& text, std::string& outError)
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output)
        {
            outError = "Write file failed: " + ToCleanPath(path);
            Log::Error(outError.c_str());
            return false;
        }

        output << text;
        return true;
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        std::ostringstream output;
        output << input.rdbuf();
        return output.str();
    }

    bool RemoveElement(std::string& text, const std::string& name)
    {
        std::string openTag = "<" + name + ">";
        std::string closeTag = "</" + name + ">";
        std::size_t open = text.find(openTag);
        if (open == std::string::npos) return false;

        std::size_t lineStart = text.rfind('\n', open);
        lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;

        std::size_t close = text.find(closeTag, open);
        if (close == std::string::npos) return false;

        std::size_t lineEnd = text.find('\n', close + closeTag.size());
        lineEnd = lineEnd == std::string::npos ? close + closeTag.size() : lineEnd + 1;
        text.erase(lineStart, lineEnd - lineStart);
        return true;
    }

    bool UsesLocalRuntimeReference(const std::string& text)
    {
        return text.find("Lib\\OrbedenCore.CSharp.dll") != std::string::npos
            || text.find("Lib/OrbedenCore.CSharp.dll") != std::string::npos;
    }

    bool ReplaceAll(std::string& text, const std::string& oldValue, const std::string& newValue)
    {
        bool changed = false;
        std::size_t position = 0;
        while ((position = text.find(oldValue, position)) != std::string::npos)
        {
            text.replace(position, oldValue.size(), newValue);
            position += newValue.size();
            changed = true;
        }

        return changed;
    }

    bool EnsureRuntimeReferenceCopyLocal(std::string& text)
    {
        if (text.find("OrbedenCore.CSharp") == std::string::npos) return false;

        return ReplaceAll(text, "<Private>false</Private>", "<Private>true</Private>");
    }

    bool RemoveLinesContaining(std::string& text, const std::string& value)
    {
        bool changed = false;
        std::size_t position = 0;
        while ((position = text.find(value, position)) != std::string::npos)
        {
            std::size_t lineStart = text.rfind('\n', position);
            lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
            std::size_t lineEnd = text.find('\n', position);
            lineEnd = lineEnd == std::string::npos ? text.size() : lineEnd + 1;
            text.erase(lineStart, lineEnd - lineStart);
            position = lineStart;
            changed = true;
        }

        return changed;
    }

    bool EnsureScriptAssemblyAotRoot(std::string& text)
    {
        std::size_t itemGroupEnd = text.find("</ItemGroup>");
        if (itemGroupEnd == std::string::npos) return false;

        bool changed = false;
        if (text.find("TrimmerRootAssembly Include=\"$(AssemblyName)\"") == std::string::npos)
        {
            text.insert(itemGroupEnd, "    <TrimmerRootAssembly Include=\"$(AssemblyName)\" />\n");
            itemGroupEnd = text.find("</ItemGroup>", itemGroupEnd);
            changed = true;
        }
        if (text.find("TrimmerRootAssembly Include=\"OrbedenCore.CSharp\"") == std::string::npos)
        {
            text.insert(itemGroupEnd, "    <TrimmerRootAssembly Include=\"OrbedenCore.CSharp\" />\n");
            changed = true;
        }
        return changed;
    }

    std::string GetDirectoryBuildPropsText()
    {
        return R"ORB(<Project>
  <PropertyGroup>
    <OutputPath>$(MSBuildThisFileDirectory)..\Managed\</OutputPath>
    <BaseIntermediateOutputPath>$(MSBuildThisFileDirectory)..\Managed\obj\</BaseIntermediateOutputPath>
    <MSBuildProjectExtensionsPath>$(BaseIntermediateOutputPath)</MSBuildProjectExtensionsPath>
  </PropertyGroup>
</Project>
)ORB";
    }

}

bool NewProjectGenerator::CreateProject(const std::string& parentDirectory,
    const std::string& projectName,
    const std::string& runtimeDllPath,
    std::string& outProjectRoot,
    std::string& outError)
{
    outProjectRoot.clear();
    outError.clear();

    if (!IsValidProjectName(projectName))
    {
        outError = "Project name must start with a letter or underscore and contain only letters, digits, or underscores.";
        Log::Error(outError.c_str());
        return false;
    }

    std::filesystem::path parentPath = Utf8Path::FromUtf8(parentDirectory);
    if (!std::filesystem::is_directory(parentPath))
    {
        outError = "Parent directory does not exist: " + parentDirectory;
        Log::Error(outError.c_str());
        return false;
    }

    std::filesystem::path runtimePath = Utf8Path::FromUtf8(runtimeDllPath);
    if (!std::filesystem::exists(runtimePath))
    {
        outError = "OrbedenCore.CSharp.dll was not found. Build OrbedenCore.vcxproj first: " + runtimeDllPath;
        Log::Error(outError.c_str());
        return false;
    }

    std::filesystem::path projectRoot = parentPath / projectName;
    if (std::filesystem::exists(projectRoot) && !std::filesystem::is_directory(projectRoot))
    {
        outError = "Project path already exists and is not a directory: " + ToCleanPath(projectRoot);
        Log::Error(outError.c_str());
        return false;
    }

    if (std::filesystem::exists(projectRoot) && !std::filesystem::is_empty(projectRoot))
    {
        outError = "Project directory already exists and is not empty: " + ToCleanPath(projectRoot);
        Log::Error(outError.c_str());
        return false;
    }

    std::filesystem::create_directories(projectRoot / "Resource");
    std::filesystem::create_directories(projectRoot / "World");
    std::filesystem::create_directories(projectRoot / "Script/Lib");
    std::filesystem::create_directories(projectRoot / "Managed");

    std::error_code copyError;
    std::filesystem::copy_file(runtimePath,
        projectRoot / "Script/Lib/OrbedenCore.CSharp.dll",
        std::filesystem::copy_options::overwrite_existing,
        copyError);
    if (copyError)
    {
        outError = "Copy OrbedenCore.CSharp.dll failed: " + copyError.message();
        Log::Error(outError.c_str());
        return false;
    }

    if (!NewProjectTemplate::GenerateProjectFiles(ToCleanPath(projectRoot), projectName, outError)) return false;

    outProjectRoot = ToCleanPath(projectRoot);
    Log::Info(("New project created: " + outProjectRoot).c_str());
    return true;
}

bool NewProjectGenerator::RepairScriptProjectBuildProps(const std::string& scriptProjectPath, std::string& outError)
{
    outError.clear();

    std::filesystem::path projectPath = Utf8Path::FromUtf8(scriptProjectPath);
    if (!std::filesystem::exists(projectPath))
    {
        outError = "Script project does not exist: " + scriptProjectPath;
        Log::Error(outError.c_str());
        return false;
    }

    std::string content = ReadTextFile(projectPath);
    bool hasLateBuildProps = content.find("<MSBuildProjectExtensionsPath>") != std::string::npos;
    bool changed = EnsureRuntimeReferenceCopyLocal(content);
    changed = RemoveLinesContaining(content, "Orbeden.ScriptGenerator.dll") || changed;
    changed = EnsureScriptAssemblyAotRoot(content) || changed;
    if (hasLateBuildProps)
    {
        RemoveElement(content, "OutputPath");
        RemoveElement(content, "BaseIntermediateOutputPath");
        RemoveElement(content, "MSBuildProjectExtensionsPath");
        changed = true;
    }

    if (changed)
    {
        if (!WriteTextFile(projectPath, content, outError)) return false;
    }

    std::filesystem::path propsPath = projectPath.parent_path() / "Directory.Build.props";
    if ((hasLateBuildProps || UsesLocalRuntimeReference(content)) && !std::filesystem::exists(propsPath))
    {
        if (!WriteTextFile(propsPath, GetDirectoryBuildPropsText(), outError)) return false;
    }

    std::filesystem::path aotExportsPath = projectPath.parent_path() / "OrbedenAotExports.cs";
    if (!std::filesystem::exists(aotExportsPath)
        && !WriteTextFile(aotExportsPath, NewProjectTemplate::GetAotExportsText(), outError)) return false;

    return true;
}
