using System.Diagnostics;
using System.Text.Json.Nodes;

internal static partial class Program
{
    /// <summary>构建第三方库并发布头文件与二进制。</summary>
    private static async Task BuildLibraries(string root, string configuration, string? vsInstall)
    {
        vsInstall = vsInstall == null ? await FindVisualStudioInstallation() : Path.GetFullPath(vsInstall);
        string cmake = Path.Combine(vsInstall, "Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "CMake", "bin", "cmake.exe");
        if (!File.Exists(cmake))
            throw new FileNotFoundException("Visual Studio CMake was not found.", cmake);
        string vendor = Path.Combine(root, "vendor");
        string publish = Path.GetFullPath(Path.Combine(root, "..", "..", "OrbedenCore", "Src", "ThirdParty"));
        string build = Path.Combine(root, "Build");
        string cmakeBuild = Path.Combine(build, "cmake", configuration);
        string stamp = Path.Combine(build, "completed-" + configuration + ".json");

        //收集构建规则与还原状态
        var inputFiles = new List<string>(Directory.EnumerateFiles(root, "*.cs"));
        inputFiles.AddRange(new[] { "OrbedenThirdParty.csproj", "dependencies.lock.json", "CMakeLists.txt", "physx-build.bat", "physx-preset.xml" }.Select(name => Path.Combine(root, name)));
        inputFiles.AddRange(Directory.EnumerateFiles(Path.Combine(root, "BuildAdapters"), "*", SearchOption.AllDirectories));
        inputFiles.AddRange(Directory.EnumerateFiles(Path.Combine(root, "licenses"), "*", SearchOption.AllDirectories));
        inputFiles.AddRange(Directory.EnumerateDirectories(vendor).Select(directory => Path.Combine(directory, ".orbeden-package.json")).Where(File.Exists));
        inputFiles.Add(Path.Combine(vendor, "glad", ".orbeden-glad.json"));
        var inputs = new JsonObject();
        foreach (string file in inputFiles.Order(StringComparer.Ordinal))
            inputs[Path.GetRelativePath(root, file).Replace('\\', '/')] = HashFile(file);
        JsonObject? previous = File.Exists(stamp) ? JsonNode.Parse(File.ReadAllText(stamp))!.AsObject() : null;
        if (previous != null && previous["vsInstall"]!.GetValue<string>() == vsInstall && JsonNode.DeepEquals(previous["inputs"], inputs) && CheckRestoredFiles(publish, previous))
        {
            Console.WriteLine($"Third-party {configuration} libraries are up to date.");
            return;
        }

        //配置并编译 CMake 库
        if (await RunProcess(cmake, root, ["-S", root, "-B", cmakeBuild, "-A", "x64", $"-DCMAKE_GENERATOR_INSTANCE={vsInstall}"]) != 0 ||
            await RunProcess(cmake, root, ["--build", cmakeBuild, "--config", configuration, "--target", "freetype", "msdfgen-core", "glfw", "imgui", "glad", "cgltf", "stb"]) != 0)
            throw new InvalidOperationException($"Third-party {configuration} CMake build failed.");

        //编译官方 PhysX 目标
        string physxConfiguration = configuration.ToLowerInvariant();
        var start = new ProcessStartInfo("cmd.exe")
        {
            Arguments = $"/d /s /c \"\"{Path.Combine(root, "physx-build.bat")}\" {physxConfiguration} \"{vsInstall}\"\"",
            WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true
        };
        using (Process process = Process.Start(start) ?? throw new InvalidOperationException("Cannot start PhysX build."))
        {
            await Task.WhenAll(process.WaitForExitAsync(), process.StandardOutput.BaseStream.CopyToAsync(Console.OpenStandardOutput()), process.StandardError.BaseStream.CopyToAsync(Console.OpenStandardError()));
            if (process.ExitCode != 0)
                throw new InvalidOperationException($"PhysX {configuration} build failed.");
        }

        //收集发布头文件与库文件
        var files = new List<(string Source, string Target)>();
        AddHeaders(files, Path.Combine(vendor, "freetype", "include"), Path.Combine(publish, "freetype", "include"));
        AddHeaders(files, Path.Combine(vendor, "msdfgen"), Path.Combine(publish, "msdfgen", "include"), onlyHeaders: true);
        AddHeaders(files, Path.Combine(vendor, "glfw", "include"), Path.Combine(publish, "glfw", "include"));
        AddHeaders(files, Path.Combine(vendor, "imgui"), Path.Combine(publish, "imgui"), onlyHeaders: true);
        AddHeaders(files, Path.Combine(vendor, "glad", "include"), Path.Combine(publish, "glad", "include"));
        AddHeaders(files, Path.Combine(vendor, "physx", "physx", "include"), Path.Combine(publish, "PhysX", "include"));
        files.Add((Path.Combine(cmakeBuild, "msdfgen-config.h"), Path.Combine(publish, "msdfgen", "include", "msdfgen", "msdfgen-config.h")));
        files.Add((Path.Combine(vendor, "cgltf", "cgltf.h"), Path.Combine(publish, "cgltf", "cgltf.h")));
        files.Add((Path.Combine(vendor, "stb", "stb_image.h"), Path.Combine(publish, "stb", "stb_image.h")));
        string libraryOutput = Path.Combine(build, "lib", configuration);
        string freetypeFile = configuration == "Debug" ? "freetyped.lib" : "freetype.lib";
        files.Add((Path.Combine(libraryOutput, freetypeFile), Path.Combine(publish, "freetype", "lib", "WindowsX64", configuration, "freetype.lib")));
        foreach (var library in new[] { ("msdfgen", "msdfgen-core.lib"), ("glfw", "glfw3dll.lib"), ("glfw", "glfw3.dll"), ("imgui", "imgui.lib"), ("glad", "glad.lib"), ("cgltf", "cgltf.lib"), ("stb", "stb.lib") })
            files.Add((Path.Combine(libraryOutput, library.Item2), Path.Combine(publish, library.Item1, "lib", "WindowsX64", configuration, library.Item2)));
        string physxOutput = Path.Combine(vendor, "physx", "physx", "bin", "win.x86_64.vc143.md", physxConfiguration);
        foreach (string library in new[] { "PhysX", "PhysXCommon", "PhysXFoundation", "PhysXExtensions", "PhysXPvdSDK", "PhysXCooking", "PhysXCharacterKinematic", "PhysXVehicle" })
        {
            string name = library + "_static_64.lib";
            files.Add((Path.Combine(physxOutput, name), Path.Combine(publish, "PhysX", "lib", "WindowsX64", configuration, name)));
        }
        foreach (var file in files)
        {
            if (!File.Exists(file.Source))
                throw new FileNotFoundException("Third-party output was not produced.", file.Source);
            GetContainedPath(publish, Path.GetRelativePath(publish, file.Target));
        }

        //清理上一版头文件并发布当前产物
        Directory.CreateDirectory(publish);
        foreach (string library in new[] { "freetype", "msdfgen", "glfw", "glad", "PhysX" })
            RemoveDirectory(Path.Combine(publish, library, "include"), publish);
        string imgui = Path.Combine(publish, "imgui");
        if (Directory.Exists(imgui))
        {
            foreach (string header in Directory.EnumerateFiles(imgui, "*.h", SearchOption.AllDirectories).ToArray())
                File.Delete(GetContainedPath(publish, Path.GetRelativePath(publish, header)));
        }
        var artifacts = new JsonObject();
        foreach (var file in files)
        {
            CopyFile(file.Source, file.Target);
            artifacts[Path.GetRelativePath(publish, file.Target).Replace('\\', '/')] = HashFile(file.Target);
        }
        WriteState(stamp, new JsonObject { ["vsInstall"] = vsInstall, ["inputs"] = inputs, ["files"] = artifacts });
        Console.WriteLine($"Third-party {configuration} libraries built and published.");
    }

    /// <summary>收集保持相对目录结构的发布头文件。</summary>
    private static void AddHeaders(List<(string Source, string Target)> files, string source, string target, bool onlyHeaders = false)
    {
        foreach (string file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            if (onlyHeaders && Path.GetExtension(file) is not (".h" or ".hpp"))
                continue;
            files.Add((file, Path.Combine(target, Path.GetRelativePath(source, file))));
        }
    }

    /// <summary>查找包含 C++ 工具链的 Visual Studio 安装。</summary>
    private static async Task<string> FindVisualStudioInstallation()
    {
        string vswhere = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Microsoft Visual Studio", "Installer", "vswhere.exe");
        if (!File.Exists(vswhere))
            throw new FileNotFoundException("vswhere was not found. Pass --vs-install explicitly.", vswhere);
        var start = new ProcessStartInfo(vswhere) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string argument in new[] { "-latest", "-products", "*", "-version", "[18.0,)", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath" })
            start.ArgumentList.Add(argument);
        using Process process = Process.Start(start) ?? throw new InvalidOperationException("Cannot start vswhere.");
        Task<string> output = process.StandardOutput.ReadToEndAsync();
        Task<string> error = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        string installation = (await output).Trim();
        string diagnostic = await error;
        if (process.ExitCode != 0 || installation.Length == 0)
            throw new InvalidOperationException($"Visual Studio 2026 C++ tools were not found. {diagnostic} Pass --vs-install explicitly.");
        return Path.GetFullPath(installation);
    }
}
