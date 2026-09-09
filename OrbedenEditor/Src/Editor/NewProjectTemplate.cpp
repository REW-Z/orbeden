#include "Editor/NewProjectTemplate.h"

#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    constexpr const char* ProjectNameToken = "{{PROJECT_NAME}}";

    //NativeAOT 导出薄层源码，Build Player 与项目创建共用。
    constexpr const char* AotExportsText = R"ORB(using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Orbeden;

// NativeAOT 只导出游戏主程序集中的入口；每个阶段在此进入托管域一次。
internal static class OrbedenAotExports
{
    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Initialize", CallConvs = [typeof(CallConvCdecl)])]
    public static void Initialize(IntPtr nativeApi) => GameScriptRuntime.Initialize(nativeApi);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Shutdown", CallConvs = [typeof(CallConvCdecl)])]
    public static void Shutdown() => GameScriptRuntime.Shutdown();

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_Update", CallConvs = [typeof(CallConvCdecl)])]
    public static void Update(float deltaTime) => GameScriptRuntime.Update(deltaTime);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_FixedUpdate", CallConvs = [typeof(CallConvCdecl)])]
    public static void FixedUpdate(float fixedDeltaTime) => GameScriptRuntime.FixedUpdate(fixedDeltaTime);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_LateUpdate", CallConvs = [typeof(CallConvCdecl)])]
    public static void LateUpdate(float deltaTime) => GameScriptRuntime.LateUpdate(deltaTime);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_EnsWorldActiveChanged", CallConvs = [typeof(CallConvCdecl)])]
    public static void EnsWorldActiveChanged(EnsId ens, byte worldActive) =>
        GameScriptRuntime.OnEnsWorldActiveChanged(ens, worldActive != 0);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_EnsDestroyed", CallConvs = [typeof(CallConvCdecl)])]
    public static void EnsDestroyed(EnsId ens) => GameScriptRuntime.OnEnsDestroyed(ens);

    [UnmanagedCallersOnly(EntryPoint = "OrbedenGame_DrawGui", CallConvs = [typeof(CallConvCdecl)])]
    public static void DrawGui() => GameScriptRuntime.DrawGUI();
}
)ORB";

    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return Utf8Path::ToUtf8(path.lexically_normal());
    }

    //替换文本中的项目名占位符。
    std::string ExpandTemplate(const std::string& text, const std::string& projectName)
    {
        std::string expanded = text;
        std::size_t position = 0;
        while ((position = expanded.find(ProjectNameToken, position)) != std::string::npos)
        {
            expanded.replace(position, std::char_traits<char>::length(ProjectNameToken), projectName);
            position += projectName.size();
        }

        return expanded;
    }

    //读取模板文本文件。
    bool ReadTextFile(const std::filesystem::path& path, std::string& outText)
    {
        std::ifstream input(path, std::ios::in | std::ios::binary);
        if (!input)
        {
            Log::Error(("Template file read failed: " + ToCleanPath(path)).c_str());
            return false;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        outText = buffer.str();
        return true;
    }

    bool WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output)
        {
            Log::Error(("Project file write failed: " + ToCleanPath(path)).c_str());
            return false;
        }

        output << text;
        return true;
    }

    //原样复制二进制模板文件。
    bool CopyBinaryFile(const std::filesystem::path& source, const std::filesystem::path& target)
    {
        if (target.has_parent_path())
        {
            std::filesystem::create_directories(target.parent_path());
        }

        std::error_code error;
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
        if (error)
        {
            Log::Error(("Template file copy failed: " + ToCleanPath(target) + ": " + error.message()).c_str());
            return false;
        }

        return true;
    }

    //判断模板文件是否需要做项目名替换。
    bool NeedsTemplateExpansion(const std::filesystem::path& relativePath)
    {
        std::filesystem::path extension = relativePath.extension();
        if (extension == ".cs" || extension == ".cpp" || extension == ".h"
            || extension == ".csproj" || extension == ".vcxproj" || extension == ".props"
            || extension == ".world" || extension == ".oeproj" || extension == ".obj"
            || extension == ".mtl" || extension == ".orbshader" || extension == ".orbinc")
        {
            return true;
        }

        //无扩展名的文本文件（.gitignore）。
        std::string fileName = Utf8Path::ToUtf8(relativePath.filename());
        return fileName == ".gitignore";
    }

    //模板中文件名随项目名变化的映射。
    std::filesystem::path MapTemplateFileName(const std::filesystem::path& relativePath, const std::string& projectName)
    {
        std::string fileName = Utf8Path::ToUtf8(relativePath);
        if (fileName == "Project.oeproj") return Utf8Path::FromUtf8(projectName + ".oeproj");
        if (fileName == "Script/Project.csproj") return Utf8Path::FromUtf8("Script/" + projectName + ".csproj");
        if (fileName == "Native/GameNative.vcxproj") return Utf8Path::FromUtf8("Native/" + projectName + "Native.vcxproj");
        return relativePath;
    }
}

//获取项目主程序集使用的固定 NativeAOT 导出薄层源码
const char* NewProjectTemplate::GetAotExportsText()
{
    return AotExportsText;
}

//把模板目录复制到空项目目录，文本文件替换项目名占位符
bool NewProjectTemplate::GenerateProjectFiles(const std::string& projectRoot,
    const std::string& projectName,
    const std::string& templateDirectory,
    std::string& outError)
{
    outError.clear();

    std::filesystem::path templateRoot = Utf8Path::FromUtf8(templateDirectory);
    if (!std::filesystem::is_directory(templateRoot))
    {
        outError = "Project template directory was not found: " + ToCleanPath(templateRoot);
        Log::Error(outError.c_str());
        return false;
    }

    std::filesystem::path root = Utf8Path::FromUtf8(projectRoot);
    std::error_code error;
    bool succeeded = true;
    std::filesystem::recursive_directory_iterator iterator(templateRoot, error);
    std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end)
    {
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_regular_file())
        {
            std::filesystem::path relative = entry.path().lexically_relative(templateRoot);
            std::filesystem::path target = root / MapTemplateFileName(relative, projectName);
            if (!NeedsTemplateExpansion(relative))
            {
                succeeded = CopyBinaryFile(entry.path(), target) && succeeded;
            }
            else
            {
                std::string text;
                if (!ReadTextFile(entry.path(), text))
                {
                    succeeded = false;
                }
                else
                {
                    succeeded = WriteTextFile(target, ExpandTemplate(text, projectName)) && succeeded;
                }
            }
        }

        iterator.increment(error);
    }

    if (error)
    {
        outError = "Project template copy failed: " + error.message();
        Log::Error(outError.c_str());
        return false;
    }

    if (succeeded)
    {
        Log::Info(("New project template generated: " + ToCleanPath(root)).c_str());
        return true;
    }

    outError = "Generate new project template failed: " + ToCleanPath(root);
    return false;
}
