#include "Editor/NewProjectGenerator.h"
#include "Editor/NewProjectTemplate.h"
#include "Editor/ProjectLayout.h"

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

    //二进制读写：工程文件要逐字节保持原样，文本模式会把已有 CRLF 再转一次。
    bool WriteTextFile(const std::filesystem::path& path, const std::string& text, std::string& outError)
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
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
        std::ifstream input(path, std::ios::in | std::ios::binary);
        std::ostringstream output;
        output << input.rdbuf();
        return output.str();
    }

    //只在内容变化时写入：无谓地刷新时间戳会让 MSBuild 认为工程过期而反复全量重建。
    bool WriteTextFileIfChanged(const std::filesystem::path& path, const std::string& text, std::string& outError)
    {
        std::error_code error;
        if (std::filesystem::exists(path, error) && ReadTextFile(path) == text) return true;
        return WriteTextFile(path, text, outError);
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

    //脚本工程的基础属性。由引擎维护并随 SDK 刷新，不要在这里写项目自己的设置。
    //EnableDefaultCompileItems 必须在这里关掉：Directory.Build.props 早于 SDK 的默认项通配，
    //只有在这里关，项目根的编译项才能完全由 SDK 的绑定目标接管。
    std::string GetDirectoryBuildPropsText()
    {
        return R"ORB(<Project>
  <PropertyGroup>
    <!--构建产物统一放在 Build/ 下，项目根只放工程文件，内容根只放内容。-->
    <OutputPath>$(MSBuildThisFileDirectory)Build\Managed\</OutputPath>
    <BaseIntermediateOutputPath>$(MSBuildThisFileDirectory)Build\Managed\obj\</BaseIntermediateOutputPath>
    <MSBuildProjectExtensionsPath>$(BaseIntermediateOutputPath)</MSBuildProjectExtensionsPath>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>
</Project>
)ORB";
    }

    //脚本工程的绑定生成入口。只做转发，实现在 SDK 里，因此引擎刷新绑定目标时无需改动项目内文件。
    std::string GetBindingsTargetsShimText()
    {
        return R"ORB(<Project>
  <!--绑定生成入口：实现在 Orbeden SDK 内，这里只把 SDK 路径转进去，不要在这里写逻辑。-->
  <PropertyGroup>
    <OrbedenSdkRoot Condition="'$(OrbedenSdkRoot)' == '' and Exists('$(MSBuildThisFileDirectory)OrbedenSdk.path')">$([System.IO.File]::ReadAllText('$(MSBuildThisFileDirectory)OrbedenSdk.path').Trim())</OrbedenSdkRoot>
  </PropertyGroup>
  <Import Project="$(OrbedenSdkRoot)/Tools/OrbedenMetaGen/Orbeden.Bindings.targets" Condition="Exists('$(OrbedenSdkRoot)/Tools/OrbedenMetaGen/Orbeden.Bindings.targets')" />
</Project>
)ORB";
    }

}

bool NewProjectGenerator::CreateProject(const std::string& parentDirectory,
    const std::string& projectName,
    const std::string& runtimeDllPath,
    const std::string& templateRoot,
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

    //内容根的初始子目录只是新建时的默认结构，之后可以随意增删改名。
    static const char* const contentFolders[] =
    {
        "Meshes", "Materials", "Textures", "Shaders", "Scenes", "Scripts",
    };
    for (const char* folder : contentFolders)
    {
        std::filesystem::create_directories(projectRoot / ProjectLayout::ContentFolder / folder);
    }

    std::filesystem::create_directories(projectRoot / ProjectLayout::LibraryFolder);
    std::filesystem::create_directories(projectRoot / ProjectLayout::ManagedFolder);

    std::error_code copyError;
    std::filesystem::copy_file(runtimePath,
        projectRoot / ProjectLayout::LibraryFolder / "OrbedenCore.CSharp.dll",
        std::filesystem::copy_options::overwrite_existing,
        copyError);
    if (copyError)
    {
        outError = "Copy OrbedenCore.CSharp.dll failed: " + copyError.message();
        Log::Error(outError.c_str());
        return false;
    }

    if (!NewProjectTemplate::GenerateProjectFiles(ToCleanPath(projectRoot), projectName, templateRoot, outError)) return false;

    if (!SyncBindingBuildFiles(ToCleanPath(projectRoot / (projectName + ".csproj")), runtimeDllPath, outError)) return false;

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
    if (content.find("Orbeden.Bindings.targets") == std::string::npos)
    {
        size_t end = content.rfind("</Project>");
        if (end == std::string::npos) { outError = "Invalid script project XML"; return false; }
        content.insert(end, "  <Import Project=\"Lib/Orbeden.Bindings.targets\" />\n");
        changed = true;
    }
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

    //基础属性由引擎维护：始终按当前 SDK 重写，否则旧项目拿不到新增项（例如关掉默认编译项通配）。
    std::filesystem::path propsPath = projectPath.parent_path() / "Directory.Build.props";
    if (!WriteTextFileIfChanged(propsPath, GetDirectoryBuildPropsText(), outError)) return false;

    return true;
}

//把当前 SDK 的 Core C# 运行库同步进脚本工程，并刷新绑定目标与 SDK 路径。
bool NewProjectGenerator::SyncRuntimeCSharpDll(const std::string& scriptProjectPath,
    const std::string& runtimeDllPath, std::string& outError)
{
    outError.clear();
    if (runtimeDllPath.empty() || !std::filesystem::exists(Utf8Path::FromUtf8(runtimeDllPath)))
    {
        outError = "OrbedenCore.CSharp.dll was not found. Build OrbedenCore.vcxproj first.";
        return false;
    }

    std::filesystem::path target = Utf8Path::FromUtf8(scriptProjectPath).parent_path() / "Lib/OrbedenCore.CSharp.dll";
    std::filesystem::create_directories(target.parent_path());

    std::error_code equivalentError;
    if (!std::filesystem::exists(target) || !std::filesystem::equivalent(Utf8Path::FromUtf8(runtimeDllPath), target, equivalentError))
    {
        std::error_code copyError;
        std::filesystem::copy_file(Utf8Path::FromUtf8(runtimeDllPath),
            target,
            std::filesystem::copy_options::overwrite_existing,
            copyError);
        if (copyError)
        {
            outError = "Copy OrbedenCore.CSharp.dll failed: " + copyError.message();
            return false;
        }
    }

    return SyncBindingBuildFiles(scriptProjectPath, runtimeDllPath, outError);
}

//同步项目所需的生成目标；项目内只留转发文件，工具与类型清单始终读取当前 SDK。
bool NewProjectGenerator::SyncBindingBuildFiles(const std::string& scriptProjectPath,
    const std::string& runtimeDllPath, std::string& outError)
{
    std::filesystem::path sdkRoot = Utf8Path::FromUtf8(runtimeDllPath).parent_path().parent_path().parent_path();
    if (!std::filesystem::exists(sdkRoot / "Tools/OrbedenMetaGen/Orbeden.Bindings.targets")
        || !std::filesystem::exists(sdkRoot / "Native/Bindings.Manifest.json"))
    {
        outError = "Binding SDK was not found. Rebuild OrbedenCore first.";
        return false;
    }
    std::filesystem::path library = Utf8Path::FromUtf8(scriptProjectPath).parent_path() / "Lib";
    return WriteTextFileIfChanged(library / "Orbeden.Bindings.targets", GetBindingsTargetsShimText(), outError)
        && WriteTextFileIfChanged(library / "OrbedenSdk.path", ToCleanPath(std::filesystem::absolute(sdkRoot)), outError);
}