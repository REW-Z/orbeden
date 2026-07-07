#include "Editor/NewProjectGenerator.h"

#include "Log/Log.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
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

    std::string GetProjectFileText(const std::string& projectName)
    {
        return "<OrbedenProject version=\"1\" name=\"" + projectName
            + "\" startupWorld=\"World/main.world\" resourceRoot=\"Resource\" scriptRoot=\"Script\" managedRoot=\"Managed\" />\n";
    }

    std::string GetScriptProjectText(const std::string& projectName)
    {
        return R"ORB(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>)ORB" + projectName + R"ORB(</AssemblyName>
    <RootNamespace>)ORB" + projectName + R"ORB(</RootNamespace>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <InvariantGlobalization>true</InvariantGlobalization>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    <AppendRuntimeIdentifierToOutputPath>false</AppendRuntimeIdentifierToOutputPath>
  </PropertyGroup>

  <ItemGroup>
    <Reference Include="OrbedenCore.CSharp">
      <HintPath>Lib\OrbedenCore.CSharp.dll</HintPath>
      <Private>false</Private>
    </Reference>
  </ItemGroup>
</Project>
)ORB";
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

    std::string GetGameModuleText(const std::string& projectName)
    {
        return R"ORB(using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OrbedenCore.CSharp;

namespace )ORB" + projectName + R"ORB(;

/// <summary>游戏 AOT 模块入口。</summary>
public static class GameModule
{
    /// <summary>初始化游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Initialize", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Initialize(IntPtr nativeApi)
    {
        OrbedenCoreRuntime.Initialize(nativeApi);
    }

    /// <summary>关闭游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Shutdown", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Shutdown()
    {
    }

    /// <summary>更新游戏模块。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Update", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_Update(float deltaTime)
    {
        _ = deltaTime;
    }

    /// <summary>绘制游戏模块 GUI。</summary>
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_DrawGui", CallConvs = [typeof(CallConvCdecl)])]
    public static void OrbedenGame_DrawGui()
    {
    }
}
)ORB";
    }

    std::string GetDefaultWorldText(const std::string& projectName)
    {
        std::string prefix = "world://" + projectName + "/main/";
        return R"ORB(<?xml version="1.0" encoding="utf-8"?>
<World version="1">
    <Ens stableId=")ORB" + prefix + R"ORB(directional_light" name="DirectionalLight">
        <Component type="SpaceComponent">
            <Field name="localPosition" type="vector3" value="0 4 0" />
            <Field name="localRotation" type="quaternion" value="0 0 0 1" />
            <Field name="localScale" type="vector3" value="1 1 1" />
        </Component>
        <Component type="DirectionalLight">
            <Field name="enabled" type="bool" value="true" />
            <Field name="direction" type="vector3" value="-0.45 -1 -0.35" />
            <Field name="color" type="color" value="1 0.96 0.86 1" />
            <Field name="intensity" type="float32" value="1.2" />
            <Field name="castShadows" type="bool" value="true" />
            <Field name="shadowBias" type="float32" value="0.004" />
            <Field name="shadowStrength" type="float32" value="0.45" />
            <Field name="shadowDistance" type="float32" value="24" />
        </Component>
    </Ens>
    <Ens stableId=")ORB" + prefix + R"ORB(camera" name="Camera">
        <Component type="SpaceComponent">
            <Field name="localPosition" type="vector3" value="5 3.2 7" />
            <Field name="localRotation" type="quaternion" value="-0.1819 0.2952 0.0574 0.9362" />
            <Field name="localScale" type="vector3" value="1 1 1" />
        </Component>
        <Component type="Camera">
            <Field name="enabled" type="bool" value="true" />
            <Field name="fieldOfView" type="float32" value="60" />
            <Field name="nearPlane" type="float32" value="0.1" />
            <Field name="farPlane" type="float32" value="1000" />
            <Field name="depth" type="float32" value="0" />
            <Field name="drawLayerMask" type="uint32" value="4294967295" />
            <Field name="clearMode" type="ClearMode" value="2" />
            <Field name="clearColor" type="color" value="0.62 0.78 0.96 1" />
        </Component>
    </Ens>
</World>
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

    std::filesystem::path parentPath(parentDirectory);
    if (!std::filesystem::is_directory(parentPath))
    {
        outError = "Parent directory does not exist: " + parentDirectory;
        Log::Error(outError.c_str());
        return false;
    }

    std::filesystem::path runtimePath(runtimeDllPath);
    if (!std::filesystem::exists(runtimePath))
    {
        outError = "OrbedenCore.CSharp.dll was not found. Build OrbedenCore.CSharp first: " + runtimeDllPath;
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

    bool succeeded = true;
    succeeded = WriteTextFile(projectRoot / (projectName + ".oeproj"), GetProjectFileText(projectName), outError) && succeeded;
    succeeded = WriteTextFile(projectRoot / "World/main.world", GetDefaultWorldText(projectName), outError) && succeeded;
    succeeded = WriteTextFile(projectRoot / "World/main.world.scripts.json", "{\n  \"scripts\": []\n}\n", outError) && succeeded;
    succeeded = WriteTextFile(projectRoot / "Script" / (projectName + ".csproj"), GetScriptProjectText(projectName), outError) && succeeded;
    succeeded = WriteTextFile(projectRoot / "Script/Directory.Build.props", GetDirectoryBuildPropsText(), outError) && succeeded;
    succeeded = WriteTextFile(projectRoot / "Script/GameModule.cs", GetGameModuleText(projectName), outError) && succeeded;
    succeeded = WriteTextFile(projectRoot / "Script/.gitignore", "bin/\nobj/\n", outError) && succeeded;
    succeeded = WriteTextFile(projectRoot / "Managed/.gitignore", "*\n!.gitignore\n", outError) && succeeded;
    if (!succeeded) return false;

    outProjectRoot = ToCleanPath(projectRoot);
    Log::Info(("New project created: " + outProjectRoot).c_str());
    return true;
}

bool NewProjectGenerator::RepairScriptProjectBuildProps(const std::string& scriptProjectPath, std::string& outError)
{
    outError.clear();

    std::filesystem::path projectPath(scriptProjectPath);
    if (!std::filesystem::exists(projectPath))
    {
        outError = "Script project does not exist: " + scriptProjectPath;
        Log::Error(outError.c_str());
        return false;
    }

    std::string content = ReadTextFile(projectPath);
    bool hasLateBuildProps = content.find("<MSBuildProjectExtensionsPath>") != std::string::npos;
    if (hasLateBuildProps)
    {
        RemoveElement(content, "OutputPath");
        RemoveElement(content, "BaseIntermediateOutputPath");
        RemoveElement(content, "MSBuildProjectExtensionsPath");
        if (!WriteTextFile(projectPath, content, outError)) return false;
    }

    std::filesystem::path propsPath = projectPath.parent_path() / "Directory.Build.props";
    if ((hasLateBuildProps || UsesLocalRuntimeReference(content)) && !std::filesystem::exists(propsPath))
    {
        if (!WriteTextFile(propsPath, GetDirectoryBuildPropsText(), outError)) return false;
    }

    return true;
}
