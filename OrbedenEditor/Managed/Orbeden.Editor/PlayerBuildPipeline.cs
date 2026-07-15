using System.Diagnostics;
using System.Xml.Linq;

namespace OrbedenEditor;

/// <summary>Editor 内建的用户游戏 NativeAOT 发布流程。</summary>
internal static class PlayerBuildPipeline
{
    private readonly record struct AotTarget(string RuntimeIdentifier, string OutputDirectory, bool UsesWindowsLibraryName);

    /// <summary>发布用户游戏 NativeAOT 静态库。</summary>
    public static bool Publish(string repositoryRoot,
        string projectRoot,
        string scriptProject,
        string configuration,
        string targetPlatform,
        out string error)
    {
        error = string.Empty;
        try
        {
            if (configuration is not ("Debug" or "Release"))
            {
                error = $"Unsupported build configuration: {configuration}";
                return false;
            }

            AotTarget target = targetPlatform switch
            {
                "WindowsX64" => new("win-x64", "windows-x64", true),
                "LinuxX64" => new("linux-x64", "linux-x64-clang", false),
                "LinuxX64Gcc" => new("linux-x64", "linux-x64-gcc", false),
                "FreeBsdX64" => new("freebsd-x64", "freebsd-x64", false),
                "Switch" => throw new InvalidOperationException("Switch NativeAOT publishing requires vendor SDK/RID integration."),
                _ => throw new InvalidOperationException($"Unsupported NativeAOT target: {targetPlatform}"),
            };

            string fullRepositoryRoot = Path.GetFullPath(repositoryRoot);
            string fullProjectRoot = Path.GetFullPath(projectRoot);
            string fullScriptProject = Path.GetFullPath(scriptProject);
            if (!Directory.Exists(fullRepositoryRoot))
            {
                error = $"Repository root was not found: {fullRepositoryRoot}";
                return false;
            }
            if (!Directory.Exists(fullProjectRoot))
            {
                error = $"Project root was not found: {fullProjectRoot}";
                return false;
            }
            if (!File.Exists(fullScriptProject))
            {
                error = $"Script project was not found: {fullScriptProject}";
                return false;
            }

            //检查 Core C# SDK 并计算发布目录。
            string sdkPath = Path.Combine(fullRepositoryRoot, "OrbedenGame", "Sdk");
            string runtimeAssembly = Path.Combine(sdkPath, "Managed", "OrbedenCore.CSharp", "OrbedenCore.CSharp.dll");
            if (!File.Exists(runtimeAssembly))
            {
                error = $"Runtime SDK assembly is missing: {runtimeAssembly}";
                return false;
            }

            string assemblyName = GetAssemblyName(fullScriptProject);
            string outputDirectory = Path.Combine(fullProjectRoot, "Aot", target.OutputDirectory, configuration);
            Directory.CreateDirectory(outputDirectory);
            string sdkProperty = Path.TrimEndingDirectorySeparator(sdkPath) + Path.DirectorySeparatorChar;

            //只使用本机 SDK/NativeAOT packs 还原发布工具链。
            string[] restoreArguments =
            [
                "restore",
                fullScriptProject,
                "-r", target.RuntimeIdentifier,
                "/p:PublishAot=true",
                "/p:NativeLib=Static",
                $"/p:OrbedenSdkPath={sdkProperty}",
                "/p:RestoreSources=",
            ];
            if (!RunDotnet(restoreArguments, out string restoreError))
            {
                error = $"NativeAOT restore failed for {targetPlatform} ({target.RuntimeIdentifier}). {restoreError}";
                return false;
            }

            //生成用户游戏 NativeAOT 静态库。
            string[] publishArguments =
            [
                "publish",
                fullScriptProject,
                "--no-restore",
                "-c", configuration,
                "-r", target.RuntimeIdentifier,
                "/p:PublishAot=true",
                "/p:NativeLib=Static",
                $"/p:OrbedenSdkPath={sdkProperty}",
                "/p:RestoreSources=",
                "-o", outputDirectory,
            ];
            if (!RunDotnet(publishArguments, out string publishError))
            {
                error = $"NativeAOT publish failed for {targetPlatform} ({target.RuntimeIdentifier}). {publishError}";
                return false;
            }

            string libraryName = target.UsesWindowsLibraryName ? $"{assemblyName}.lib" : $"lib{assemblyName}.a";
            string libraryPath = Path.Combine(outputDirectory, libraryName);
            if (!File.Exists(libraryPath))
            {
                error = $"NativeAOT publish did not produce: {libraryPath}";
                return false;
            }

            Console.WriteLine($"Published NativeAOT static library: {libraryPath}");
            return true;
        }
        catch (Exception exception)
        {
            error = exception.Message;
            return false;
        }
    }

    //读取脚本工程程序集名。
    private static string GetAssemblyName(string scriptProject)
    {
        XDocument project = XDocument.Load(scriptProject);
        string? assemblyName = project.Descendants()
            .FirstOrDefault(element => element.Name.LocalName == "AssemblyName")
            ?.Value
            .Trim();
        return string.IsNullOrWhiteSpace(assemblyName)
            ? Path.GetFileNameWithoutExtension(scriptProject)
            : assemblyName;
    }

    //执行一次 dotnet 命令并转发标准输出。
    private static bool RunDotnet(IReadOnlyList<string> arguments, out string error)
    {
        ProcessStartInfo startInfo = new()
        {
            FileName = "dotnet",
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };
        foreach (string argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }
        startInfo.Environment["DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE"] = "1";
        startInfo.Environment["DOTNET_SKIP_FIRST_TIME_EXPERIENCE"] = "1";

        using Process process = new() { StartInfo = startInfo };
        if (!process.Start())
        {
            error = "dotnet process could not be started.";
            return false;
        }

        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync();
        Task<string> standardError = process.StandardError.ReadToEndAsync();
        process.WaitForExit();
        Task.WaitAll(standardOutput, standardError);

        string output = standardOutput.Result;
        string errorOutput = standardError.Result;
        if (!string.IsNullOrWhiteSpace(output)) Console.Write(output);
        if (!string.IsNullOrWhiteSpace(errorOutput)) Console.Error.Write(errorOutput);

        error = process.ExitCode == 0
            ? string.Empty
            : string.IsNullOrWhiteSpace(errorOutput) ? $"dotnet exited with code {process.ExitCode}." : errorOutput.Trim();
        return process.ExitCode == 0;
    }
}
