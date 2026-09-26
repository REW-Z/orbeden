#include "Editor/EditorSystem.h"

#include "Log/Log.h"
#include "Profiler/Profiler.h"
#include "Editor/EditorIcons.h"
#include "Editor/NewProjectGenerator.h"
#include "Editor/PlayerContentCooker.h"
#include "Editor/ProjectLayout.h"
#include "Editor/ProjectUpgrader.h"
#include "InputManager/InputManager.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "Platform/ExecutablePath.h"
#include "Rendering/RenderSystem.h"
#include "ResourceManager/ResourceManager.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace
{
    //日志唤醒回调：新日志写入时叫醒编辑器主循环
    void WakeEditorLoop(void* context)
    {
        static_cast<EditorSystem*>(context)->RequestRepaint();
    }

    struct PlayerTargetPlatformInfo
    {
        const char* displayName;
        const char* scriptName;
        const char* aotDirectory;
        bool available = false;
    };

    //目前只有 Windows 走完整发布流程；其余目标保留入口但置灰。
    constexpr std::array<PlayerTargetPlatformInfo, 5> PlayerTargetPlatforms =
    {
        PlayerTargetPlatformInfo { "Windows x64", "WindowsX64", "windows-x64", true },
        PlayerTargetPlatformInfo { "Linux x64", "LinuxX64", "linux-x64-clang" },
        PlayerTargetPlatformInfo { "Linux x64 GCC", "LinuxX64Gcc", "linux-x64-gcc" },
        PlayerTargetPlatformInfo { "FreeBSD x64", "FreeBsdX64", "freebsd-x64" },
        PlayerTargetPlatformInfo { "Switch", "Switch", "switch" },
    };

    enum class ToolbarIcon
    {
        Play,
        Pause,
        Stop,
    };

    //绘制工具栏图标按钮
    bool DrawToolbarIconButton(const char* id, ToolbarIcon icon, const ImVec2& size)
    {
        bool clicked = ImGui::Button(id, size);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        float32 width = max.x - min.x;
        float32 height = max.y - min.y;
        float32 side = std::min(width, height);
        ImVec2 center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

        if (icon == ToolbarIcon::Play)
        {
            float32 triangleWidth = side * 0.42f;
            float32 triangleHeight = side * 0.48f;
            drawList->AddTriangleFilled(
                ImVec2(center.x - triangleWidth * 0.38f, center.y - triangleHeight * 0.5f),
                ImVec2(center.x - triangleWidth * 0.38f, center.y + triangleHeight * 0.5f),
                ImVec2(center.x + triangleWidth * 0.52f, center.y),
                color);
        }
        else if (icon == ToolbarIcon::Pause)
        {
            float32 barWidth = side * 0.13f;
            float32 barHeight = side * 0.48f;
            float32 gap = side * 0.12f;
            drawList->AddRectFilled(
                ImVec2(center.x - gap * 0.5f - barWidth, center.y - barHeight * 0.5f),
                ImVec2(center.x - gap * 0.5f, center.y + barHeight * 0.5f),
                color,
                1.0f);
            drawList->AddRectFilled(
                ImVec2(center.x + gap * 0.5f, center.y - barHeight * 0.5f),
                ImVec2(center.x + gap * 0.5f + barWidth, center.y + barHeight * 0.5f),
                color,
                1.0f);
        }
        else
        {
            float32 squareSize = side * 0.42f;
            drawList->AddRectFilled(
                ImVec2(center.x - squareSize * 0.5f, center.y - squareSize * 0.5f),
                ImVec2(center.x + squareSize * 0.5f, center.y + squareSize * 0.5f),
                color,
                1.5f);
        }

        return clicked;
    }

    const PlayerTargetPlatformInfo& GetPlayerTargetPlatformInfo(int32 index)
    {
        if (index < 0 || index >= static_cast<int32>(PlayerTargetPlatforms.size()))
        {
            return PlayerTargetPlatforms[0];
        }

        return PlayerTargetPlatforms[static_cast<usize>(index)];
    }

    std::string GetNativeAotLibraryName(const PlayerTargetPlatformInfo& target, const std::string& assemblyName)
    {
        if (std::strcmp(target.scriptName, "WindowsX64") == 0)
        {
            return assemblyName + ".lib";
        }

        return "lib" + assemblyName + ".a";
    }

#if defined(NDEBUG)
    constexpr const char* BuildConfiguration = "Release";
#else
    constexpr const char* BuildConfiguration = "Debug";
#endif

    //Player AOT 产物相对项目根的目录。发布方在 Orbeden.Editor 的 PlayerBuildPipeline 里，
    //两处必须一致，否则构建完成后会在这里找不到产物。
    constexpr const char* PlayerAotDirectory = "Build/Aot";

    //Player 打包目录相对项目根，必须与 OrbedenGame.vcxproj 的 OutDir 一致。
    constexpr const char* PlayerPackageDirectory = "Build/windows-x64/bin";

    std::string ToCleanPath(const std::filesystem::path& path)
    {
        return Utf8Path::ToUtf8(path.lexically_normal());
    }

    void CopyToBuffer(char* buffer, std::size_t bufferSize, const std::string& value)
    {
        if (!buffer || bufferSize == 0) return;

        std::size_t copySize = std::min(bufferSize - 1, value.size());
        std::memcpy(buffer, value.data(), copySize);
        buffer[copySize] = '\0';
    }

    List<std::string> GetChildDirectories(const std::string& directory)
    {
        List<std::string> result;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(Utf8Path::FromUtf8(directory), error))
        {
            if (error) break;
            if (!entry.is_directory()) continue;

            result.push_back(Utf8Path::ToUtf8(entry.path().filename()));
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        std::ostringstream output;
        output << input.rdbuf();
        return output.str();
    }

    std::string GetXmlTagValue(const std::string& text, const std::string& name)
    {
        std::string openTag = "<" + name + ">";
        std::string closeTag = "</" + name + ">";
        std::size_t begin = text.find(openTag);
        if (begin == std::string::npos) return std::string();

        begin += openTag.size();
        std::size_t end = text.find(closeTag, begin);
        return end == std::string::npos ? std::string() : text.substr(begin, end - begin);
    }

    std::string Quote(const std::string& value)
    {
        return "\"" + value + "\"";
    }

    std::string FindFirstCsproj(const std::filesystem::path& directory)
    {
        if (!std::filesystem::is_directory(directory)) return std::string();

        List<std::string> projects;
        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
        {
            if (error) break;
            if (entry.is_regular_file() && entry.path().extension() == ".csproj")
            {
                projects.push_back(ToCleanPath(entry.path()));
            }
        }

        std::sort(projects.begin(), projects.end());
        return projects.empty() ? std::string() : projects.front();
    }

    std::string GetParentDirectory(const std::string& path)
    {
        std::filesystem::path value = Utf8Path::FromUtf8(path);
        return ToCleanPath(value.has_parent_path() ? value.parent_path() : std::filesystem::current_path());
    }

    bool FileExists(const std::string& path)
    {
        return !path.empty() && std::filesystem::exists(Utf8Path::FromUtf8(path));
    }

    //判断脚本源是否比程序集更新。脚本可以放在项目下任何目录，因此以项目根为扫描范围；
    //生成目录里的时间戳变化不代表脚本源变化，整棵子树直接跳过。
    bool IsProjectScriptBuildOutdated(const std::string& projectRoot, const std::string& assemblyPath)
    {
        if (projectRoot.empty() || assemblyPath.empty()) return false;
        std::filesystem::path assembly = Utf8Path::FromUtf8(assemblyPath);
        if (!std::filesystem::exists(assembly)) return true;

        std::error_code error;
        std::filesystem::file_time_type assemblyTime = std::filesystem::last_write_time(assembly, error);
        if (error) return true;

        //内容根之外都是构建生成物或工程文件，时间戳变化不代表脚本源变化。
        const char* const excludedPrefixes[] =
        {
            "Build/", "Lib/", "Legacy/", ".vs/", ".git/",
        };

        std::filesystem::path root = Utf8Path::FromUtf8(projectRoot);
        std::filesystem::recursive_directory_iterator iterator(root, error);
        std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end)
        {
            const std::filesystem::directory_entry& entry = *iterator;
            std::string relative = ToCleanPath(entry.path().lexically_relative(root)) + "/";

            bool excluded = false;
            for (const char* prefix : excludedPrefixes)
            {
                if (relative.compare(0, std::char_traits<char>::length(prefix), prefix) == 0)
                {
                    excluded = true;
                    break;
                }
            }

            if (excluded)
            {
                if (entry.is_directory()) iterator.disable_recursion_pending();
            }
            else if (entry.is_regular_file())
            {
                std::filesystem::path extension = entry.path().extension();
                if (extension == ".cs" || extension == ".csproj" || extension == ".props" || extension == ".targets")
                {
                    std::filesystem::file_time_type sourceTime = std::filesystem::last_write_time(entry.path(), error);
                    if (error) return true;
                    if (sourceTime > assemblyTime) return true;
                }
            }

            iterator.increment(error);
        }

        return false;
    }

    bool ScriptProjectUsesLocalRuntimeDll(const std::string& csproj)
    {
        std::string content = ReadTextFile(Utf8Path::FromUtf8(csproj));
        return content.find("Lib\\OrbedenCore.CSharp.dll") != std::string::npos
            || content.find("Lib/OrbedenCore.CSharp.dll") != std::string::npos;
    }

    bool RefreshLocalRuntimeDllReference(const std::string& csproj, const std::string& runtimeDllPath, std::string& outError)
    {
        outError.clear();
        if (!ScriptProjectUsesLocalRuntimeDll(csproj)) return true;

        return NewProjectGenerator::SyncRuntimeCSharpDll(csproj, runtimeDllPath, outError);
    }

    //读取环境变量为文件系统路径：未设置时返回空路径。
    //Windows 下走宽字符 API，避免经过 ANSI 代码页丢掉非 ASCII 字符。
    std::filesystem::path GetEnvironmentPath(const char* name)
    {
#if defined(_WIN32)
        //变量名是 ASCII，值可能是任意 Unicode，因此按宽字符读取。
        std::wstring wideName(name, name + std::strlen(name));
        wchar_t* value = nullptr;
        std::size_t size = 0;
        if (_wdupenv_s(&value, &size, wideName.c_str()) != 0 || !value) return std::filesystem::path();

        std::filesystem::path result(value);
        std::free(value);
        return result;
#else
        const char* value = std::getenv(name);
        return value ? Utf8Path::FromUtf8(value) : std::filesystem::path();
#endif
    }

    //在子进程上执行命令并收集全部标准输出。
    //二进制模式读取：管道里的字节原样返回，不做换行与代码页转换，vswhere 的 -utf8 输出可以直接当 UTF-8 用。
    std::string CaptureCommandOutput(const std::string& command)
    {
#if defined(_WIN32)
        FILE* pipe = _popen(command.c_str(), "rb");
#else
        FILE* pipe = popen(command.c_str(), "r");
#endif
        if (!pipe) return std::string();

        std::string output;
        std::array<char, 512> buffer {};
        std::size_t readSize = 0;
        while ((readSize = std::fread(buffer.data(), 1, buffer.size(), pipe)) > 0)
        {
            output.append(buffer.data(), readSize);
        }

#if defined(_WIN32)
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return output;
    }

    //取输出的第一行：vswhere 的 -format value 一行一个结果。
    std::string FirstOutputLine(const std::string& output)
    {
        std::size_t begin = output.find_first_not_of("\r\n");
        if (begin == std::string::npos) return std::string();

        std::size_t end = output.find_first_of("\r\n", begin);
        return end == std::string::npos ? output.substr(begin) : output.substr(begin, end - begin);
    }

    //命令行里的可执行文件路径必须使用系统首选分隔符：cmd 不认 Utf8Path::ToUtf8 的正斜杠通用格式。
    std::string ToNativeUtf8(const std::filesystem::path& path)
    {
        std::u8string bytes = path.u8string();
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    bool IsRegularFile(const std::filesystem::path& path)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    }

    //按 Visual Studio 的安装布局取某个安装根内的 MSBuild.exe。
    //Current 是随更新重建的链接名，同级的版本目录是布局变化时的兜底。
    std::filesystem::path FindMSBuildUnderRoot(const std::filesystem::path& visualStudioRoot)
    {
        std::filesystem::path current = visualStudioRoot / "MSBuild/Current/Bin/MSBuild.exe";
        if (IsRegularFile(current)) return current;

        std::error_code error;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(visualStudioRoot / "MSBuild", error))
        {
            std::filesystem::path candidate = entry.path() / "Bin/MSBuild.exe";
            if (IsRegularFile(candidate)) return candidate;
        }

        return std::filesystem::path();
    }

    //PATH 的条目分隔符：Windows 用分号，POSIX 用冒号。
#if defined(_WIN32)
    constexpr char PathVariableSeparator = ';';
#else
    constexpr char PathVariableSeparator = ':';
#endif

    //PATH 上的 MSBuild.exe：从 Developer Command Prompt 启动编辑器时直接命中。
    std::filesystem::path FindMSBuildOnPath()
    {
        std::string pathList = Utf8Path::ToUtf8(GetEnvironmentPath("PATH"));
        std::size_t begin = 0;
        while (begin < pathList.size())
        {
            std::size_t end = pathList.find(PathVariableSeparator, begin);
            if (end == std::string::npos) end = pathList.size();

            std::string entry = pathList.substr(begin, end - begin);
            //PATH 允许用引号包住带空格的目录。
            if (entry.size() >= 2 && entry.front() == '"' && entry.back() == '"')
            {
                entry = entry.substr(1, entry.size() - 2);
            }
            if (!entry.empty())
            {
                std::filesystem::path candidate = Utf8Path::FromUtf8(entry) / "MSBuild.exe";
                if (IsRegularFile(candidate)) return candidate;
            }

            begin = end + 1;
        }

        return std::filesystem::path();
    }

    //VS 安装器的组件 ID：x64 C++ 生成工具。用它把没装 C++ 工作负载的实例挡在查询之外。
    constexpr const char* VcToolsComponentId = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64";

    //vswhere.exe 是 VS 2017+ 的官方查询入口，固定装在 Installer 目录，与安装盘符、版本和版本号都无关。
    std::filesystem::path GetVsWherePath()
    {
        std::filesystem::path programFilesX86 = GetEnvironmentPath("ProgramFiles(x86)");
        if (programFilesX86.empty()) return std::filesystem::path();

        return programFilesX86 / "Microsoft Visual Studio/Installer/vswhere.exe";
    }

    //vswhere 查询：返回输出的第一行（-format value 一行一个结果）；vswhere 缺失时返回空串。
    std::string QueryVswhere(const std::string& arguments)
    {
        std::filesystem::path vsWhere = GetVsWherePath();
        if (!IsRegularFile(vsWhere)) return std::string();

        std::string command = Quote(ToNativeUtf8(vsWhere)) + " " + arguments + " -utf8";
        return FirstOutputLine(CaptureCommandOutput(command));
    }

    //确认某个 VS 实例带了工程要求的平台工具集。
    //工具集随 VS 版本单调递增（2022 是 v143、2026 是 v145），最新实例都没有就意味着别的实例也没有。
    //MSBuild 下的 VC 布局认不出来时（一个工具集目录都没找到）视为通过，避免把未知布局误判成缺工具集。
    bool HasPlatformToolset(const std::filesystem::path& visualStudioRoot, const std::string& toolset)
    {
        if (toolset.empty()) return true;

        std::error_code error;
        bool sawAnyToolset = false;
        for (const std::filesystem::directory_entry& targets : std::filesystem::directory_iterator(visualStudioRoot / "MSBuild/Microsoft/VC", error))
        {
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(targets.path() / "Platforms/x64/PlatformToolsets", error))
            {
                sawAnyToolset = true;
                if (entry.path().filename() == toolset) return true;
            }
        }

        return !sawAnyToolset;
    }

    //从 SDK 的共享属性表里读工程要求的平台工具集；读不到时返回空串，查找时跳过工具集校验。
    //路径与游戏工程的导入位置一致：Sdk/Native/Orbeden.Native.props。
    std::string ReadRequiredPlatformToolset(const std::filesystem::path& sdkRoot)
    {
        std::filesystem::path props = sdkRoot / "Native/Orbeden.Native.props";
        if (!IsRegularFile(props)) return std::string();

        return GetXmlTagValue(ReadTextFile(props), "PlatformToolset");
    }

    //定位 MSBuild.exe；失败时报出可操作的原因——退化成裸名字只会得到 cmd 的“不是内部或外部命令”。
    //Visual Studio 的安装盘符、版本和版本号都由用户自选，写死安装路径必然在别的机器上失效。
    bool FindMSBuild(std::string& outPath, std::string& outError, const std::string& requiredToolset)
    {
        //1) 显式指定优先：指错了要立刻失败，不要静默换成另一套工具链。
        std::filesystem::path overridePath = GetEnvironmentPath("ORBEDEN_MSBUILD");
        if (!overridePath.empty())
        {
            if (!IsRegularFile(overridePath))
            {
                outError = "ORBEDEN_MSBUILD does not point to an existing file: " + ToNativeUtf8(overridePath);
                return false;
            }

            outPath = ToNativeUtf8(overridePath.lexically_normal());
            return true;
        }

        //同一次编辑器会话内工具链不会变化，只缓存成功结果：失败后重试仍会重新查找。
        static std::string cachedPath;
        if (!cachedPath.empty())
        {
            outPath = cachedPath;
            return true;
        }

        //2) vswhere 是 VS 2017+ 的官方查询入口。先只接受装了 x64 C++ 生成工具的实例，查不到再放宽条件。
        std::string installPath = QueryVswhere("-latest -prerelease -products * -requires "
            + std::string(VcToolsComponentId) + " -property installationPath -format value");
        if (installPath.empty())
        {
            installPath = QueryVswhere("-latest -prerelease -products * -property installationPath -format value");
        }

        //3) 校验实例自带的工具集：缺 v145 这类目标工具集时立刻说清楚，不必等到 MSB8020。
        std::filesystem::path visualStudioRoot = Utf8Path::FromUtf8(installPath);
        bool toolsetMissing = !visualStudioRoot.empty() && !HasPlatformToolset(visualStudioRoot, requiredToolset);

        std::filesystem::path resolved;
        if (!toolsetMissing) resolved = FindMSBuildUnderRoot(visualStudioRoot);

        //4) PATH 上的 MSBuild.exe：从 Developer Command Prompt 启动编辑器时命中，那里的工具链是用户自己摆好的。
        if (resolved.empty()) resolved = FindMSBuildOnPath();

        if (resolved.empty())
        {
            outError = toolsetMissing
                ? "The latest Visual Studio instance (" + ToNativeUtf8(visualStudioRoot)
                    + ") provides no platform toolset '" + requiredToolset
                    + "'. Install the matching C++ workload, or point ORBEDEN_MSBUILD at a suitable MSBuild.exe."
                : "MSBuild.exe was not found. Install Visual Studio with the C++ workload, "
                    "or start the editor from a Developer Command Prompt, then retry.";
            return false;
        }

        //路径先归一化再转原生分隔符，随后整个命令按 UTF-8 传给 cmd。
        cachedPath = ToNativeUtf8(resolved.lexically_normal());
        outPath = cachedPath;
        return true;
    }

    //判断项目根是否包含游戏 C++ 工程文件。
    bool HasNativeVcxProject(const std::string& nativeRoot)
    {
        if (nativeRoot.empty()) return false;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(Utf8Path::FromUtf8(nativeRoot), error))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".vcxproj") return true;
        }
        return false;
    }

}

EditorSystem::EditorSystem(Application& application, const char* startupExecutablePath)
    : app(application)
    , project(application)
    , executablePath(startupExecutablePath ? startupExecutablePath : "")
    , editorScene(application, managedBridge)
{
    //日志一到达就唤醒消息循环：面板因此不必靠连续重绘保持刷新。
    //注册与摘除都跟着本对象的生命周期走——回调用的是 this，对象析构后
    //退出阶段（Quit、内存分析）的日志还会触发它，那时就是空悬指针了。
    Log::SetWakeHandler(&WakeEditorLoop, this);

    previousInputEnabled = InputManager::IsEnabled();
    InputManager::SetEnabled(false);
    //编辑器以编辑态启动：只渲染场景面板的离屏目标，游戏相机不直接画主 framebuffer
    if (RenderSystem* renderSystem = app.GetSystem<RenderSystem>())
        renderSystem->SetMainFramebufferRendering(false);
    if (!editorGUI.Initialize(app.GetWindow()))
    {
        Log::Error("EditorSystem initialize failed: EditorGUI initialize failed.");
        return;
    }
    SetDialogDirectory(ToCleanPath(std::filesystem::current_path()));
    CopyToBuffer(newProjectNameBuffer, sizeof(newProjectNameBuffer), "NewGame");

    std::filesystem::path executableDirectory = ExecutablePath::GetDirectory(executablePath);
    //组件图标与 Templates 一样随 exe 分发
    EditorIcons::SetDirectory(executableDirectory / "Resources" / "Icons");
    std::filesystem::path managedDirectory = executableDirectory / "Managed";
    EditorClrHostConfig clrConfig;
    clrConfig.runtimeConfigPath = ToCleanPath(executableDirectory / "OrbedenEditor.runtimeconfig.json");
    clrConfig.componentAssemblyPath = ToCleanPath(managedDirectory / "Orbeden.Editor.dll");
    if (clrHost.Initialize(clrConfig))
    {
        managedBridge.Initialize(clrHost,
            *this,
            editorGUI,
            panelManager,
            editorScene.GetGizmoApi(),
            executablePath);
    }
}

EditorSystem::~EditorSystem()
{
    //先摘掉日志唤醒，后面析构过程中再写日志就不会打到正在析构的对象上
    Log::SetWakeHandler(nullptr, nullptr);

    //Play 期间面板被隐藏，先把可见性恢复成 Play 前的记录再保存布局，
    //否则在 Play 中退出编辑器会把“全部隐藏”写进项目
    if (playMode.IsPlaying()) panelManager.ApplyLayout(playPanelLayout);
    SaveEditorLayout();
    app.SetPaused(false);
    app.SetSimulationEnabled(false);
    playMode.Stop();
    app.GetWorld().Clear();
    std::string nativeUnloadError;
    nativeGameModule.Unload(nativeUnloadError);
    managedBridge.UnloadGameAssembly();
    managedBridge.Shutdown();
    clrHost.Shutdown();
    InputManager::SetEnabled(previousInputEnabled);

    //独立窗口共享主上下文的字体图集，必须在主 ImGui 上下文销毁前释放
    panelManager.DestroyFloatingOsWindows();
    editorGUI.Shutdown();
}

void EditorSystem::Update(World& world, float deltaTime)
{
    PROFILE("Editor/Update");

    if (!pendingProjectFile.empty())
    {
        std::string path = std::exchange(pendingProjectFile, std::string());
        ProjectVersionProbe probe;
        std::string error;
        if (!EditorProject::ProbeProjectFile(path, probe, error)) projectStatus = error;
        else if (probe.status == ProjectVersionStatus::Newer)
            projectStatus = "This project requires a newer version of Orbeden.";
        else if (probe.status == ProjectVersionStatus::Outdated)
        {
            pendingUpgrade = probe;
            upgradeError.clear();
            upgradeProjectDialog = true;
        }
        else
        {
            RequestStop();
            SaveEditorLayout();
            if (project.LoadProjectFile(path)) FinishProjectLoad("Loaded", "opened");
            else projectStatus = project.GetLastError();
        }
        RequestRepaint();
    }

    //Play 期间的改动不落盘，脏标记跟随 Play 状态；每帧赋值，异常路径也能自愈
    world.SetDirtyTrackingEnabled(!playMode.IsPlaying());

    float32 mouseWheel = editorGUI.ConsumeSceneMouseWheel();
    if (!playMode.IsPlaying())
    {
        editorScene.Update(world, deltaTime, mouseWheel);
        //空闲编辑器不出帧，聚焦动画得自己把下一帧要过来，否则会卡在半路
        if (editorScene.IsAnimatingFocus()) RequestRepaint();
    }

    //场景改为离屏渲染后主窗口不再被场景填充，需要在渲染前清空
    if (!playMode.IsPlaying()) editorGUI.ClearMainFramebuffer();

    //按场景视口当前可见性维护离屏目标，Play 期间同样需要释放
    editorScene.RefreshSceneViewTarget(world);
}

//请求编辑器重绘并唤醒事件循环
void EditorSystem::RequestRepaint()
{
    if (repaintRequested.exchange(true, std::memory_order_acq_rel)) return;
    if (IWindow* window = app.GetWindow()) window->WakeEventLoop();
}

//获取并清除编辑器重绘请求
bool EditorSystem::TakeRepaintRequest()
{
    return repaintRequested.exchange(false, std::memory_order_acq_rel);
}

//判断编辑器是否需要连续重绘
bool EditorSystem::NeedsContinuousRepaint() const
{
    return continuousRepaint || (playMode.IsPlaying() && !app.IsPaused());
}

void EditorSystem::RenderEditorGUI()
{
    PROFILE("Editor/GUI");

    if (!editorGUI.IsInitialized()) return;

    editorGUI.BeginFrame();
    ProcessEditorShortcuts();
    UpdateWindowTitle();
    DrawMainMenuBar();
    if (project.HasProject()) DrawPlayToolbar();
    //状态栏必须在面板之前：边栏会收缩视口工作区，停靠宿主随后按收缩后的区域布局
    DrawStatusBar();
    DrawProjectDialog();
    DrawNewProjectDialog();
    DrawUpgradeProjectDialog();
    if (!playMode.IsPlaying()) editorScene.PruneSelection(app.GetWorld());
    {
        PROFILE("Editor/Panels");
        if (project.HasProject()) panelManager.DrawPanels();
        else panelManager.DrawStandalonePanel("welcome");
    }

    //场景视口由 Scene 面板在自身内容区内绘制，Play 期间取消鼠标交互
    if (playMode.IsPlaying()) editorScene.CancelInteraction();

    //独立窗口创建或销毁发生在绘制之后，需要唤醒下一轮事件循环
    if (panelManager.TakeRepaintRequest()) RequestRepaint();

    //更新连续重绘状态。除了活动控件与独立窗口，模态暗化层的淡入淡出也要连续帧：
    //它每帧才推进一点（涨 6/s、退 10/s），隔帧绘制会一直卡在中间。
    //判据是「还没走到目标值」而不是「模态开着」——涨满只要 0.17 秒，
    //按模态开着算会让弹窗停留的整段时间都跑满帧率，白烧渲染。
    ImGuiContext* context = ImGui::GetCurrentContext();
    float32 dimTarget = ImGui::GetTopMostPopupModal() != nullptr ? 1.0f : 0.0f;
    continuousRepaint = ImGui::IsAnyItemActive()
        || panelManager.IsAnyFloatingItemActive()
        || (context != nullptr && context->DimBgRatio != dimTarget);
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        || ImGui::IsMouseReleased(ImGuiMouseButton_Right)
        || ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
    {
        RequestRepaint();
    }

    editorGUI.Render();
}

void EditorSystem::RequestOpenProjectDialog()
{
    OpenProjectDialog();
    RequestRepaint();
}

/// <summary>排队打开最近项目，加载与版本升级沿用编辑器现有流程。</summary>
void EditorSystem::RequestOpenProjectFile(const std::string& path)
{
    if (path.empty()) return;
    pendingProjectFile = path;
    projectStatus.clear();
    RequestRepaint();
}

void EditorSystem::RequestNewProjectDialog()
{
    OpenNewProjectDialog();
    RequestRepaint();
}

void EditorSystem::RequestSaveCurrentWorld()
{
    SaveCurrentWorld();
}

//打开项目内的另一个场景
bool EditorSystem::OpenWorld(const std::string& relativeKey)
{
    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        return false;
    }
    if (playMode.IsPlaying()) RequestStop();

    if (!project.OpenWorld(relativeKey))
    {
        projectStatus = project.GetLastError();
        Log::Error(projectStatus.c_str());
        return false;
    }

    editorScene.ClearSceneState();
    projectStatus = "Opened scene: " + relativeKey;
    RequestRepaint();
    return true;
}

//项目加载成功后的统一收尾。Load 与 New Project 两条路径共用，
//延迟执行的升级路径也必须走这里，否则会漏掉布局恢复与 Inspector 程序集刷新。
void EditorSystem::FinishProjectLoad(const std::string& successLabel, const std::string& pendingNativeLabel)
{
    editorScene.ClearSceneState();
    dialogError.clear();
    SetDialogDirectory(project.GetProjectRoot());
    projectStatus = successLabel + ": " + project.GetProjectRoot();

    std::string runtimeSyncError;
    if (!SyncProjectRuntimeCSharpDll(runtimeSyncError))
    {
        projectStatus += " Core C# sync failed: " + runtimeSyncError;
        Log::Warning(runtimeSyncError.c_str());
    }

    if (!BuildNativeGameModule(false))
    {
        projectStatus = "Project " + pendingNativeLabel + ". Native scripts still need to be compiled. " + projectStatus;
    }

    ApplyEditorLayout();
    RefreshInspectorGameAssembly();
}

//加载一个已通过版本闸门的项目
void EditorSystem::LoadProjectFromFolder(const std::string& folder)
{
    RequestStop();
    SaveEditorLayout();
    if (project.LoadProjectFolder(folder))
    {
        FinishProjectLoad("Loaded", "opened");
    }
    else
    {
        dialogError = project.GetLastError();
        projectStatus = dialogError;
    }
}

//对 pendingUpgrade 指向的项目执行升级
bool EditorSystem::RunProjectUpgrade(std::string& outError)
{
    outError.clear();
    if (pendingUpgrade.projectFilePath.empty())
    {
        outError = "No project is pending upgrade.";
        return false;
    }

    std::string templateRoot = GetProjectTemplateDirectory();
    if (templateRoot.empty())
    {
        outError = "Project template directory was not found. Rebuild OrbedenEditor.";
        return false;
    }

    ProjectUpgrader::UpgradeRequest request;
    request.projectRoot = pendingUpgrade.projectRoot;
    request.projectName = pendingUpgrade.projectName;
    request.projectFilePath = pendingUpgrade.projectFilePath;
    request.runtimeDllPath = FindRuntimeCSharpDll();
    request.templateRoot = templateRoot;

    return ProjectUpgrader::UpgradeProject(request, outError);
}

//项目升级弹窗：载入时发现版本不一致才出现，只有"升级"和"退出"两个选择。
void EditorSystem::DrawUpgradeProjectDialog()
{
    if (upgradeProjectDialog)
    {
        ImGui::OpenPopup("Upgrade Project");
        upgradeProjectDialog = false;
    }

    ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Upgrade Project", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    ImGui::TextUnformatted("This project was created by an older version of Orbeden.");
    ImGui::Spacing();
    ImGui::Text("Project: %s", pendingUpgrade.projectFilePath.c_str());
    ImGui::Text("Project version: %u    Current version: %u", pendingUpgrade.storedVersion, OrbedenProjectVersion);
    ImGui::Spacing();
    ImGui::TextWrapped("Upgrading rebuilds everything outside Content and refreshes the engine SDK and the build "
        "scaffold. Content is left untouched.");
    ImGui::Spacing();

    if (!upgradeError.empty())
    {
        ImGui::TextWrapped("%s", upgradeError.c_str());
        ImGui::Spacing();
    }

    if (ImGui::Button("Upgrade", ImVec2(140.0f, 0.0f)))
    {
        std::string error;
        if (RunProjectUpgrade(error))
        {
            std::string folder = ToCleanPath(Utf8Path::FromUtf8(pendingUpgrade.projectFilePath).parent_path());
            pendingUpgrade = ProjectVersionProbe();
            upgradeError.clear();
            LoadProjectFromFolder(folder);
            ImGui::CloseCurrentPopup();
        }
        else
        {
            //版本号未写入，可以留在弹窗里重试，也可以直接退出。
            upgradeError = error;
            projectStatus = error;
            Log::Error(error.c_str());
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Exit", ImVec2(140.0f, 0.0f)))
    {
        //什么都不做：不加载项目，也不改动编辑器当前状态。
        pendingUpgrade = ProjectVersionProbe();
        upgradeError.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorSystem::RequestBuildScripts()
{
    if (playMode.IsPlaying())
    {
        RequestStop();
    }

    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        return;
    }

    std::string csproj = GetProjectScriptProjectPath();
    if (csproj.empty())
    {
        projectStatus = "No C# project found in script root.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string projectRepairError;
    if (!NewProjectGenerator::RepairScriptProjectBuildProps(csproj, projectRepairError))
    {
        projectStatus = projectRepairError;
        return;
    }

    std::string runtimeRefreshError;
    if (!RefreshLocalRuntimeDllReference(csproj, FindRuntimeCSharpDll(), runtimeRefreshError))
    {
        projectStatus = runtimeRefreshError;
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string command = "dotnet build " + Quote(csproj) + " -c Debug";
    if (RunCommand(command, "Build Game C#"))
    {
        RefreshInspectorGameAssembly();
        projectStatus = "Built Game C#: " + GetProjectGameAssemblyPath();
    }
}

void EditorSystem::RequestBuildNative()
{
    BuildNativeGameModule(true);
}

/// <summary>重置后重新读取场景和脚本；加载失败时保持不可保存状态。</summary>
bool EditorSystem::ReloadProjectContent()
{
    if (!project.HasProject() || playMode.IsPlaying()) return false;

    project.MarkWorldPendingReload();
    managedBridge.UnloadGameAssembly();
    editorScene.ClearSceneState();
    app.GetWorld().Clear();
    //镜像只写磁盘文件，已加载的资源对象仍攥着旧源码与旧 GPU 包装；
    //不清资源注册表的话，重载世界拿到的还是旧对象，改过的 Shader 不会重新导入。
    if (RenderSystem* renderSystem = app.GetSystem<RenderSystem>())
    {
        renderSystem->InvalidateResourceCaches();
    }
    ResourceManager::Shutdown();
    if (!BuildNativeGameModule(false)) return false;
    if (!project.IsWorldLoaded() && !project.ReloadWorld())
    {
        projectStatus = project.GetLastError();
        return false;
    }

    std::string csproj = GetProjectScriptProjectPath();
    if (!csproj.empty() && !RunCommand("dotnet build " + Quote(csproj) + " -c Debug", "Build Game C#")) return false;
    RefreshInspectorGameAssembly();
    RequestRepaint();
    return true;
}

bool EditorSystem::BuildNativeGameModule(bool saveWorldBeforeReload)
{
    if (playMode.IsPlaying()) RequestStop();
    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        return false;
    }

    //原生工程与导出层直接放在项目根；没有工程文件即视为纯托管项目。
    std::string nativeRoot = project.GetProjectRoot();
    if (!HasNativeVcxProject(nativeRoot))
    {
        if (!nativeGameModule.IsLoaded()) return true;
        project.MarkWorldPendingReload();
        app.GetWorld().Clear();
        std::string unloadError;
        if (!nativeGameModule.Unload(unloadError))
        {
            projectStatus = unloadError;
            return false;
        }
        return project.ReloadWorld();
    }

    //项目切换时先移除旧模块，避免新 World 误用同名旧类型。
    if (!saveWorldBeforeReload && nativeGameModule.IsLoaded())
    {
        project.MarkWorldPendingReload();
        app.GetWorld().Clear();
        std::string unloadError;
        if (!nativeGameModule.Unload(unloadError))
        {
            projectStatus = unloadError;
            return false;
        }
    }

    //手动热重载时记录当前 World 实际依赖的游戏模块组件类型。
    bool preserveLoadedWorld = saveWorldBeforeReload && project.IsWorldLoaded();
    List<std::string> requiredTypes;
    std::unordered_set<std::string> requiredTypeSet;
    if (preserveLoadedWorld) app.GetWorld().ForEachEns([&](Ens& ens)
        {
            for (Component* component : ens.GetComponents())
            {
                Type* type = component ? component->GetType() : nullptr;
                if (!type || type->GetModuleOwner() != &nativeGameModule) continue;
                if (requiredTypeSet.insert(type->GetName()).second) requiredTypes.push_back(type->GetName());
            }
        });

    if (preserveLoadedWorld && !SaveCurrentWorld()) return false;
    if (saveWorldBeforeReload && !preserveLoadedWorld)
    {
        Log::Info("Startup World is waiting for Native scripts; skipped the pre-build save.");
    }

    std::string repositoryRoot = FindRepositoryRoot();
    if (repositoryRoot.empty())
    {
        projectStatus = "Repository root was not found.";
        return false;
    }

    std::filesystem::path buildDirectory = Utf8Path::FromUtf8(nativeRoot) / ProjectLayout::NativeBuildFolder;
    //定位唯一的游戏 C++ 工程文件，模块名随工程文件名。
    std::filesystem::path vcxProject;
    std::error_code scanError;
    for (const auto& entry : std::filesystem::directory_iterator(Utf8Path::FromUtf8(nativeRoot), scanError))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".vcxproj")
        {
            vcxProject = entry.path();
            break;
        }
    }
    if (vcxProject.empty())
    {
        projectStatus = "Native project is missing a .vcxproj: " + nativeRoot;
        Log::Error(projectStatus.c_str());
        return false;
    }

    std::filesystem::path sdkPath = Utf8Path::FromUtf8(repositoryRoot) / "OrbedenEditor/Sdk";

    //工具链找不到时直接给出报错，不要拼出一条一定会失败的命令行。
    std::string msbuildPath;
    std::string msbuildError;
    if (!FindMSBuild(msbuildPath, msbuildError, ReadRequiredPlatformToolset(sdkPath)))
    {
        projectStatus = "Build Game C++ failed: " + msbuildError;
        Log::Error(projectStatus.c_str());
        return false;
    }

    std::string sdkRoot = ToCleanPath(sdkPath);
    std::string buildCommand = Quote(msbuildPath)
        + " " + Quote(ToCleanPath(vcxProject))
        + " -p:Configuration=" + BuildConfiguration + " -p:Platform=x64"
        + " -p:OrbedenSdkRoot=" + Quote(sdkRoot);

    //构建期间继续保留旧 shadow DLL，源输出可以直接覆盖
    if (!RunCommand(buildCommand, "Build Game C++")) return false;

    std::string modulePath = ToCleanPath(buildDirectory
        / (project.GetProjectName() + ProjectLayout::ModuleNameSuffix + ".dll"));
    std::string shadowDirectory = ToCleanPath(Utf8Path::FromUtf8(project.GetManagedRootPath()) / ".native-pie");

    //清空全部模块实例后替换 DLL，再从 .world 恢复字段和挂载顺序
    project.MarkWorldPendingReload();
    app.GetWorld().Clear();
    std::string reloadError;
    bool loaded = nativeGameModule.Reload(modulePath, shadowDirectory, requiredTypes, reloadError);
    if (!project.ReloadWorld())
    {
        projectStatus = project.GetLastError();
        return false;
    }
    editorScene.ClearSceneState();

    if (!loaded)
    {
        projectStatus = "Game C++ reload failed. " + reloadError;
        Log::Error(projectStatus.c_str());
        return false;
    }

    projectStatus = "Built Game C++: " + modulePath;
    RequestRepaint();
    return true;
}

void EditorSystem::RequestPlay()
{
    if (playMode.IsPlaying()) return;
    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        return;
    }

    std::string assemblyPath = GetProjectGameAssemblyPath();
    if (!FileExists(assemblyPath) || IsProjectScriptBuildOutdated(project.GetProjectRoot(), assemblyPath))
    {
        RequestBuildScripts();
        assemblyPath = GetProjectGameAssemblyPath();
        if (IsProjectScriptBuildOutdated(project.GetProjectRoot(), assemblyPath))
        {
            projectStatus = "C# build failed or output is out of date.";
            Log::Error(projectStatus.c_str());
            return;
        }
    }

    if (!FileExists(assemblyPath))
    {
        projectStatus = "Game DLL is missing. Build Game C# first: " + assemblyPath;
        Log::Error(projectStatus.c_str());
        return;
    }

    SaveEditorLayout();
    if (!SaveCurrentWorld())
    {
        return;
    }

    //Play 期间的改动不落盘，进入前先关掉脏标记记录，避免同帧窗口漏标
    app.GetWorld().SetDirtyTrackingEnabled(false);
    editorScene.EnterPlayMode(app.GetWorld());

    std::filesystem::path shadowDirectory = Utf8Path::FromUtf8(project.GetManagedRootPath()) / ".pie";
    std::string runtimeAssemblyPath = FindRuntimeCSharpDll();
    ScriptSystem* scriptSystem = app.GetSystem<ScriptSystem>();
    if (!scriptSystem
        || !playMode.Start(*scriptSystem, clrHost, assemblyPath, runtimeAssemblyPath,
            ToCleanPath(shadowDirectory), GetManagedDependencyDirectories()))
    {
        projectStatus = playMode.GetLastError();
        editorScene.RestoreCamera(app.GetWorld());
        return;
    }

    managedBridge.LoadGameAssembly(playMode.GetShadowAssemblyPath());
    app.SetPaused(false);
    app.SetSimulationEnabled(true);
    InputManager::SetEnabled(true);

    //记录面板布局并隐藏全部编辑器面板，停止 Play 后恢复。
    panelManager.WriteLayout(playPanelLayout);
    panelManager.HideAllPanels();
    //Play 期间游戏相机直接画主 framebuffer，编辑态则只渲染场景面板的离屏目标
    if (RenderSystem* renderSystem = app.GetSystem<RenderSystem>())
        renderSystem->SetMainFramebufferRendering(true);

    projectStatus = "Play-In-Editor started.";
    RequestRepaint();
}

void EditorSystem::RequestStop()
{
    app.CancelWorldLoad();
    if (!playMode.IsPlaying()) return;

    app.SetPaused(false);
    app.SetSimulationEnabled(false);
    playMode.Stop();
    InputManager::SetEnabled(false);
    if (RenderSystem* renderSystem = app.GetSystem<RenderSystem>())
        renderSystem->SetMainFramebufferRendering(false);

    //恢复 Play 前的面板布局。
    panelManager.ApplyLayout(playPanelLayout);

    managedBridge.UnloadGameAssembly();
    if (project.HasProject())
    {
        if (project.ReloadWorld())
        {
            editorScene.ExitPlayMode(app.GetWorld());
        }
        else
        {
            editorScene.ExitPlayMode(app.GetWorld());
            projectStatus = project.GetLastError();
            return;
        }
    }

    RefreshInspectorGameAssembly();
    projectStatus = "Play-In-Editor stopped.";
    RequestRepaint();
}

void EditorSystem::RequestBuildPlayer()
{
    if (playMode.IsPlaying())
    {
        RequestStop();
    }

    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string repoRoot = FindRepositoryRoot();
    if (repoRoot.empty())
    {
        projectStatus = "Repository root was not found.";
        Log::Error(projectStatus.c_str());
        return;
    }

    const PlayerTargetPlatformInfo& target = GetPlayerTargetPlatformInfo(selectedPlayerTargetPlatform);
    if (!target.available)
    {
        projectStatus = "The selected target platform is not available yet: " + std::string(target.displayName);
        Log::Error(projectStatus.c_str());
        return;
    }
    if (std::strcmp(target.scriptName, "Switch") == 0)
    {
        projectStatus = "Switch Player build requires vendor SDK/RID integration. No DLL fallback was attempted.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string scriptProject = GetProjectScriptProjectPath();
    if (scriptProject.empty())
    {
        projectStatus = "Build Player failed: no C# project found in script root.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string assemblyName = GetProjectGameAssemblyName();
    if (assemblyName.empty())
    {
        projectStatus = "Build Player failed: game assembly name was not found.";
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string projectRepairError;
    if (!NewProjectGenerator::RepairScriptProjectBuildProps(scriptProject, projectRepairError))
    {
        projectStatus = "Build Player project migration failed: " + projectRepairError;
        Log::Error(projectStatus.c_str());
        return;
    }

    if (!RefreshLocalRuntimeDllReference(scriptProject, FindRuntimeCSharpDll(), projectRepairError))
    {
        projectStatus = "Build Player SDK refresh failed: " + projectRepairError;
        return;
    }

    std::string publishError;
    if (!managedBridge.PublishGameAot(repoRoot,
        project.GetProjectRoot(),
        scriptProject,
        BuildConfiguration,
        target.scriptName,
        publishError))
    {
        projectStatus = "Build Player AOT failed for " + std::string(target.displayName) + ".";
        if (!publishError.empty()) projectStatus += " " + publishError;
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string aotLibraryName = GetNativeAotLibraryName(target, assemblyName);
    //命令行参数使用原生分隔符路径：ToCleanPath 输出正斜杠，cmd 内建命令（copy）无法解析。
    std::filesystem::path aotLibraryFile = Utf8Path::FromUtf8(project.GetProjectRoot())
        / PlayerAotDirectory
        / target.aotDirectory
        / BuildConfiguration
        / Utf8Path::FromUtf8(aotLibraryName);
    std::string aotLibraryPath = aotLibraryFile.string();
    if (!FileExists(aotLibraryPath))
    {
        projectStatus = "Build Player failed: NativeAOT library was not found: " + aotLibraryPath;
        Log::Error(projectStatus.c_str());
        return;
    }

    //AOT 共享库与导入库同目录，构建完成后拷贝到 Player 输出目录。
    std::filesystem::path aotLibrary = Utf8Path::FromUtf8(aotLibraryPath);
    std::filesystem::path aotDll = aotLibrary;
    aotDll.replace_extension(".dll");

    std::string playerProject = ToCleanPath(Utf8Path::FromUtf8(repoRoot) / "OrbedenGame/OrbedenGame.vcxproj");
    if (!std::filesystem::exists(Utf8Path::FromUtf8(playerProject)))
    {
        projectStatus = "Player project was not found: " + playerProject;
        Log::Error(projectStatus.c_str());
        return;
    }

    std::filesystem::path sdkPath = Utf8Path::FromUtf8(repoRoot) / "OrbedenEditor/Sdk";

    //Player 只链接 SDK 预编译的 Core 静态库；缺失时直接失败，不触发 Core 源码编译。
    std::filesystem::path coreStaticLibrary = sdkPath / "Native/WindowsX64" / BuildConfiguration / "OrbedenCoreStatic.lib";
    if (!std::filesystem::exists(coreStaticLibrary))
    {
        projectStatus = "Build Player failed: Orbeden Core static library was not found: "
            + ToCleanPath(coreStaticLibrary) + ". Build OrbedenCore (x64|" + BuildConfiguration + ") to refresh the SDK.";
        Log::Error(projectStatus.c_str());
        return;
    }

    //工具链找不到时直接给出报错，不要拼出一条一定会失败的命令行。
    std::string msbuildPath;
    std::string msbuildError;
    if (!FindMSBuild(msbuildPath, msbuildError, ReadRequiredPlatformToolset(sdkPath)))
    {
        projectStatus = "Build Player failed: " + msbuildError;
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string buildCommand = Quote(msbuildPath)
        + " " + Quote(playerProject)
        + " -p:Configuration=" + BuildConfiguration + " -p:Platform=x64"
        + " -p:OrbedenProjectDir=" + Quote(project.GetProjectRoot())
        + " -p:OrbedenGameAotLib=" + Quote(aotLibraryPath)
        + " -p:OrbedenGameAotDll=" + Quote(ToCleanPath(aotDll));
    if (!RunCommand(buildCommand, "Build Player"))
    {
        projectStatus = "Build Player failed for " + std::string(target.displayName) + ".";
        return;
    }

    //Player 只读打包产物：先把内容根内的资源导入后写成二进制，再同步进发布目录。
    std::string packageError;
    if (!CookPlayerContent(packageError))
    {
        projectStatus = "Build Player packaging failed: " + packageError;
        Log::Error(projectStatus.c_str());
        return;
    }

    std::string packageRoot = ToCleanPath(Utf8Path::FromUtf8(project.GetProjectRoot()) / PlayerPackageDirectory);
    if (!SyncPlayerPackage(packageRoot, packageError))
    {
        projectStatus = "Build Player packaging failed: " + packageError;
        Log::Error(projectStatus.c_str());
        return;
    }

    projectStatus = "Built Player (" + std::string(target.displayName) + "): " + packageRoot + "/OrbedenGame.exe";
}

//把内容根内的资源 cook 到 ResourceCache，并重建当前场景
bool EditorSystem::CookPlayerContent(std::string& error)
{
    error.clear();

    //导入会写进进程级资源表，先按打开项目的既有流程清空，结束后再从磁盘重建场景。
    if (RenderSystem* renderSystem = app.GetSystem<RenderSystem>())
    {
        renderSystem->InvalidateResourceCaches();
    }

    project.MarkWorldPendingReload();
    app.GetWorld().Clear();
    ResourceManager::Shutdown();
    PathDefines::SetContentRoot(project.GetContentRootPath());

    std::string cacheRoot = ToCleanPath(Utf8Path::FromUtf8(project.GetProjectRoot()) / ProjectLayout::PlayerResourceCacheFolder);
    bool cooked = PlayerContentCooker::Cook(project.GetContentRootPath(), cacheRoot, error);

    //cook 导入的全部资源都是一次性的，释放后由场景重载重新取用。
    ResourceManager::Shutdown();
    editorScene.ClearSceneState();

    bool reloaded = project.ReloadWorld();
    if (!cooked) return false;
    if (!reloaded)
    {
        error = project.GetLastError();
        return false;
    }

    return true;
}

//清空包内 Content 后同步 cook 产物，再把 .oeproj 复制到包根
bool EditorSystem::SyncPlayerPackage(const std::string& packageRoot, std::string& error)
{
    error.clear();

    std::filesystem::path cacheRoot = Utf8Path::FromUtf8(project.GetProjectRoot()) / ProjectLayout::PlayerResourceCacheFolder;
    std::filesystem::path packageContentRoot = Utf8Path::FromUtf8(packageRoot) / ProjectLayout::ContentFolder;

    //先删干净，避免上一次打包残留的产物留在包里。
    std::error_code code;
    std::filesystem::remove_all(packageContentRoot, code);
    std::filesystem::copy(cacheRoot, packageContentRoot,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, code);
    if (code)
    {
        error = "Cooked content could not be copied into the package: " + ToCleanPath(packageContentRoot);
        return false;
    }

    //Player 从包根的项目文件读取启动场景。
    std::filesystem::path projectFile = Utf8Path::FromUtf8(project.GetProjectFilePath());
    std::filesystem::copy_file(projectFile, Utf8Path::FromUtf8(packageRoot) / projectFile.filename(),
        std::filesystem::copy_options::overwrite_existing, code);
    if (code)
    {
        error = "Project file could not be copied into the package: " + ToCleanPath(projectFile);
        return false;
    }

    return true;
}

bool EditorSystem::IsPlaying() const
{
    return playMode.IsPlaying();
}

bool EditorSystem::HasProject() const
{
    return project.HasProject();
}

const std::string& EditorSystem::GetProjectName() const
{
    return project.GetProjectName();
}

const std::string& EditorSystem::GetProjectRoot() const
{
    return project.GetProjectRoot();
}

std::string EditorSystem::GetProjectContentRootPath() const
{
    return project.GetContentRootPath();
}

std::string EditorSystem::GetProjectManagedRootPath() const
{
    return project.GetManagedRootPath();
}

std::string EditorSystem::GetProjectNativeBuildPath() const
{
    return project.GetNativeBuildPath();
}

std::string EditorSystem::GetWorldPath() const
{
    return project.GetWorldPath();
}

const std::string& EditorSystem::GetProjectStatusText() const
{
    return projectStatus;
}

int32 EditorSystem::GetPlayerTargetPlatformCount() const
{
    return static_cast<int32>(PlayerTargetPlatforms.size());
}

int32 EditorSystem::GetSelectedPlayerTargetPlatformIndex() const
{
    return selectedPlayerTargetPlatform;
}

const char* EditorSystem::GetPlayerTargetPlatformName(int32 index) const
{
    return GetPlayerTargetPlatformInfo(index).displayName;
}

bool EditorSystem::IsPlayerTargetPlatformAvailable(int32 index) const
{
    return GetPlayerTargetPlatformInfo(index).available;
}

void EditorSystem::SetSelectedPlayerTargetPlatformIndex(int32 index)
{
    if (index < 0 || index >= GetPlayerTargetPlatformCount()) return;
    if (!IsPlayerTargetPlatformAvailable(index)) return;

    selectedPlayerTargetPlatform = index;
}

const char* EditorSystem::GetSelectedPlayerTargetPlatformName() const
{
    return GetPlayerTargetPlatformInfo(selectedPlayerTargetPlatform).displayName;
}

World& EditorSystem::GetWorld()
{
    return app.GetWorld();
}

const World& EditorSystem::GetWorld() const
{
    return app.GetWorld();
}

EditorScene& EditorSystem::GetEditorScene()
{
    return editorScene;
}

const EditorScene& EditorSystem::GetEditorScene() const
{
    return editorScene;
}

//绘制一个托管面板
void EditorSystem::DrawManagedPanel(int32 handle)
{
    const List<EnsId>& selection = editorScene.GetSelectedEnsList();
    std::string stableIds;
    for (EnsId ens : selection)
    {
        stableIds += editorScene.GetStableId(ens);
        stableIds.push_back('\0');
    }
    managedBridge.DrawPanel(handle,
        editorScene.GetSelectedEns(),
        selection.empty() ? nullptr : selection.data(),
        static_cast<int32>(selection.size()),
        stableIds,
        editorScene.GetSelectedStableId());
}

//设置托管面板可见状态
void EditorSystem::SetManagedPanelVisible(int32 handle, bool visible)
{
    managedBridge.SetPanelVisible(handle, visible);
}

std::string EditorSystem::GetProjectScriptProjectPath() const
{
    //脚本工程直接放在项目根，优先用约定名，找不到再取第一个 .csproj。
    std::filesystem::path projectRoot = Utf8Path::FromUtf8(project.GetProjectRoot());
    std::filesystem::path expected = projectRoot / Utf8Path::FromUtf8(project.GetProjectName() + ".csproj");
    if (!projectRoot.empty() && std::filesystem::exists(expected)) return ToCleanPath(expected);

    return FindFirstCsproj(projectRoot);
}

std::string EditorSystem::GetProjectGameAssemblyName() const
{
    std::string csproj = GetProjectScriptProjectPath();
    if (csproj.empty()) return std::string();

    std::string content = ReadTextFile(Utf8Path::FromUtf8(csproj));
    std::string assemblyName = GetXmlTagValue(content, "AssemblyName");
    if (!assemblyName.empty()) return assemblyName;

    return Utf8Path::ToUtf8(Utf8Path::FromUtf8(csproj).stem());
}

std::string EditorSystem::GetProjectGameAssemblyPath() const
{
    std::string assemblyName = GetProjectGameAssemblyName();
    if (assemblyName.empty()) return std::string();

    return ToCleanPath(Utf8Path::FromUtf8(project.GetManagedRootPath()) / Utf8Path::FromUtf8(assemblyName + ".dll"));
}


bool EditorSystem::RefreshInspectorGameAssembly()
{
    if (!project.HasProject())
    {
        managedBridge.UnloadGameAssembly();
        return false;
    }

    std::string assemblyPath = GetProjectGameAssemblyPath();
    if (!FileExists(assemblyPath))
    {
        managedBridge.LoadGameAssembly(std::string());
        return false;
    }

    managedBridge.LoadGameAssembly(assemblyPath);
    return true;
}

std::string EditorSystem::FindRepositoryRoot() const
{
    List<std::filesystem::path> starts;
    starts.push_back(ExecutablePath::GetDirectory(executablePath));
    starts.push_back(std::filesystem::current_path());
    if (project.HasProject())
    {
        starts.push_back(Utf8Path::FromUtf8(project.GetProjectRoot()));
    }

    for (std::filesystem::path start : starts)
    {
        start = std::filesystem::absolute(start);
        while (!start.empty())
        {
            if (std::filesystem::exists(start / "orbeden.slnx"))
            {
                return ToCleanPath(start);
            }

            std::filesystem::path parent = start.parent_path();
            if (parent == start) break;
            start = parent;
        }
    }

    return std::string();
}

std::string EditorSystem::FindRuntimeCSharpDll() const
{
    constexpr const char* RuntimeDllRelativePath = "Sdk/Managed/OrbedenCore.CSharp/OrbedenCore.CSharp.dll";

    List<std::filesystem::path> candidates;
    std::filesystem::path executableDirectory = ExecutablePath::GetDirectory(executablePath);
    candidates.push_back(executableDirectory / RuntimeDllRelativePath);

    std::filesystem::path parentDirectory = executableDirectory.parent_path();
    if (!parentDirectory.empty())
    {
        candidates.push_back(parentDirectory / RuntimeDllRelativePath);
        std::filesystem::path editorDirectory = parentDirectory.parent_path();
        if (!editorDirectory.empty())
        {
            candidates.push_back(editorDirectory / RuntimeDllRelativePath);
        }
    }

    std::string repoRoot = FindRepositoryRoot();
    if (!repoRoot.empty())
    {
        candidates.push_back(Utf8Path::FromUtf8(repoRoot) / "OrbedenEditor" / RuntimeDllRelativePath);
        candidates.push_back(Utf8Path::FromUtf8(repoRoot) / "OrbedenGame" / RuntimeDllRelativePath);
    }

    for (const std::filesystem::path& path : candidates)
    {
        if (std::filesystem::exists(path))
        {
            return ToCleanPath(path);
        }
    }

    return std::string();
}

//Debug模式下同步Core C#运行库到当前游戏项目
bool EditorSystem::SyncProjectRuntimeCSharpDll(std::string& outError) const
{
    outError.clear();
#if defined(NDEBUG)
    return true;
#else
    if (!project.HasProject()) return true;

    std::string runtimeDll = FindRuntimeCSharpDll();
    if (runtimeDll.empty())
    {
        outError = "OrbedenCore.CSharp.dll was not found. Build OrbedenCore.vcxproj first.";
        return false;
    }

    std::string scriptProject = GetProjectScriptProjectPath();
    if (scriptProject.empty())
    {
        outError = "Project script project is empty.";
        return false;
    }

    if (!RefreshLocalRuntimeDllReference(scriptProject, runtimeDll, outError))
    {
        return false;
    }

    Log::Info(("Synchronized Core C# runtime: " + ToCleanPath(Utf8Path::FromUtf8(scriptProject).parent_path() / "Lib")).c_str());
    return true;
#endif
}

//获取 Play/Inspector 需要复制的托管依赖目录
List<std::string> EditorSystem::GetManagedDependencyDirectories() const
{
    List<std::string> directories;
    std::string runtimeDll = FindRuntimeCSharpDll();
    if (!runtimeDll.empty())
    {
        directories.push_back(ToCleanPath(Utf8Path::FromUtf8(runtimeDll).parent_path()));
    }

    return directories;
}

bool EditorSystem::RunCommand(const std::string& command, const char* actionName)
{
    Log::Info(("RunCommand: " + command).c_str());
#if defined(_WIN32)
    std::wstring nativeCommand = Utf8Path::FromUtf8(command).wstring();
    //必须保留这层外引号：_wsystem 走 cmd /C，当命令以引号开头且以引号结尾时，
    //cmd 会剥掉首尾两个引号再解析；命令内的带空格路径（如 "C:\Program Files\...\MSBuild.exe"）
    //因此失去引号保护而被按空格截断。外层再包一对引号，让 cmd 剥掉外层、保留内层。
    //切勿删除：删除后 Build Game C++ / Build Player 会报 'C:\Program' 不是内部或外部命令。
    int result = _wsystem((L"\"" + nativeCommand + L"\"").c_str());
#else
    int result = std::system(command.c_str());
#endif
    if (result == 0)
    {
        projectStatus = std::string(actionName) + " succeeded.";
        return true;
    }

    projectStatus = std::string(actionName) + " failed.";
    Log::Error(projectStatus.c_str());
    return false;
}

bool EditorSystem::SaveCurrentWorld()
{
    if (playMode.IsPlaying())
    {
        projectStatus = "Stop Play-In-Editor before saving.";
        Log::Warning(projectStatus.c_str());
        return false;
    }

    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        Log::Warning(projectStatus.c_str());
        return false;
    }

    World& world = app.GetWorld();
    bool saved = false;
    {
        //编辑器的临时候选相机不写进场景，摘除与恢复都不算场景改动
        World::DirtySuppressionScope suppression(world);
        bool hadEditorCamera = editorScene.RemoveCameraForSerialization(world);
        bool managedSaved = managedBridge.SaveProjectState();
        bool worldSaved = managedSaved && project.SaveWorld();
        saved = managedSaved && worldSaved;
        projectStatus = saved ? ("Saved: " + project.GetWorldPath())
            : (managedSaved ? project.GetLastError() : "Managed project data save failed.");
        if (hadEditorCamera) editorScene.RestoreCamera(world);
    }
    editorScene.CancelInteraction();

    //保存成功才清脏：失败时保留未保存标记
    if (saved) world.ClearDirty();
    return saved;
}

//保存当前编辑器布局
void EditorSystem::SaveEditorLayout()
{
    if (!project.HasProject()) return;

    EditorLayoutState layout;
    panelManager.WriteLayout(layout);
    editorScene.WriteLayout(layout);
    project.SaveEditorLayout(layout);
}

//应用当前项目编辑器布局
void EditorSystem::ApplyEditorLayout()
{
    if (!project.HasProject()) return;

    const EditorLayoutState& layout = project.GetEditorLayout();
    panelManager.ApplyLayout(layout);
    editorScene.ApplyLayout(layout, app.GetWorld());
}

void EditorSystem::OpenProjectDialog()
{
    openProjectDialog = true;
    dialogError.clear();
    if (project.HasProject())
    {
        SetDialogDirectory(project.GetProjectRoot());
    }
}

void EditorSystem::OpenNewProjectDialog()
{
    newProjectDialog = true;
    dialogError.clear();
    if (project.HasProject())
    {
        SetDialogDirectory(GetParentDirectory(project.GetProjectRoot()));
    }
    else
    {
        SetDialogDirectory(ToCleanPath(std::filesystem::current_path()));
    }

    if (newProjectNameBuffer[0] == '\0')
    {
        CopyToBuffer(newProjectNameBuffer, sizeof(newProjectNameBuffer), "NewGame");
    }
}

//获取编辑器快捷键表，菜单显示文本与按键分发共用这一份
const List<EditorShortcut>& EditorSystem::GetEditorShortcuts()
{
    //整张表是静态常量：菜单提示与实际键位不会各写一份而漂移
    static const List<EditorShortcut> shortcuts =
    {
        { "Project", "Save", "Ctrl+S", ImGuiKey_S, true, false, false, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.SaveCurrentWorld(); } },
        { "Edit", "Undo", "Ctrl+Z", ImGuiKey_Z, true, false, false, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.managedBridge.Undo(); } },
        { "Edit", "Redo", "Ctrl+Y", ImGuiKey_Y, true, false, false, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.managedBridge.Redo(); } },

        //重做的别名键位，菜单里只显示 Ctrl+Y
        { nullptr, nullptr, "Ctrl+Shift+Z", ImGuiKey_Z, true, true, false, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.managedBridge.Redo(); } },

        //重命名与删除都由选择系统指名持有选中项的面板：EnsView 改/删 Ens，Project 改/删资源
        { "Edit", "Rename", "F2", ImGuiKey_F2, false, false, false, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.managedBridge.RequestRenameSelected(); } },
        { "Edit", "Delete", "Delete", ImGuiKey_Delete, false, false, false, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.managedBridge.RequestDeleteSelected(); } },

        //复制同样指名面板，粘贴则发给当前聚焦的面板：粘贴不要求面板已有选中项
        { "Edit", "Copy", "Ctrl+C", ImGuiKey_C, true, false, false, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.managedBridge.RequestCopySelected(); } },
        { "Edit", "Paste", "Ctrl+V", ImGuiKey_V, true, false, false, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.managedBridge.RequestPasteSelected(); } },

        //激活状态由 EnsView 处理：切换选中 Ens 的 localActive，层级生效状态随之刷新
        { "Edit", "Toggle Active", "Alt+Shift+A", ImGuiKey_A, false, true, true, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.managedBridge.RequestToggleActiveSelected(); } },

        //重新导入同样由选择系统指名面板；全量版本不依赖选择，直接派发
        { "Project", "Reimport", "Ctrl+R", ImGuiKey_R, true, false, false, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.managedBridge.RequestReimportSelected(); } },
        { "Project", "Reimport All", "Ctrl+Shift+R", ImGuiKey_R, true, true, false, EditorShortcutScope::Global,
            [](EditorSystem& editor) { editor.managedBridge.RequestReimportAll(); } },

        //手柄模式与坐标系跟随鼠标位置，鼠标不在场景视口内时不生效
        { nullptr, nullptr, "W", ImGuiKey_W, false, false, false, EditorShortcutScope::SceneView,
            [](EditorSystem& editor) { editor.editorScene.SetGizmoMode(EditorGizmoMode::Move); } },
        { nullptr, nullptr, "E", ImGuiKey_E, false, false, false, EditorShortcutScope::SceneView,
            [](EditorSystem& editor) { editor.editorScene.SetGizmoMode(EditorGizmoMode::Rotate); } },
        { nullptr, nullptr, "R", ImGuiKey_R, false, false, false, EditorShortcutScope::SceneView,
            [](EditorSystem& editor) { editor.editorScene.SetGizmoMode(EditorGizmoMode::Scale); } },
        { nullptr, nullptr, "X", ImGuiKey_X, false, false, false, EditorShortcutScope::SceneView,
            [](EditorSystem& editor)
            {
                editor.editorScene.SetGizmoOrientation(
                    editor.editorScene.GetGizmoOrientation() == EditorGizmoOrientation::Global
                        ? EditorGizmoOrientation::Local : EditorGizmoOrientation::Global);
            } },

        //取消拖拽只在手柄拖拽期间有意义
        { nullptr, nullptr, "Esc", ImGuiKey_Escape, false, false, false, EditorShortcutScope::Gizmo,
            [](EditorSystem& editor) { editor.editorScene.CancelInteraction(); } },
    };
    return shortcuts;
}

//分发编辑器快捷键；Play 期间整套失效，按键交给游戏
void EditorSystem::ProcessEditorShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    //文本控件编辑时保留 ImGui 自身的 Undo，其余情况才走编辑器快捷键
    if (playMode.IsPlaying() || io.WantTextInput) return;

    for (const EditorShortcut& shortcut : GetEditorShortcuts())
    {
        if (shortcut.ctrl != io.KeyCtrl || shortcut.shift != io.KeyShift || shortcut.alt != io.KeyAlt) continue;
        if (shortcut.scope == EditorShortcutScope::SceneView && !editorScene.IsMouseOverSceneView()) continue;
        if (shortcut.scope == EditorShortcutScope::Gizmo && !editorScene.IsGizmoDragging()) continue;
        if (!ImGui::IsKeyPressed(shortcut.key, false)) continue;

        if (shortcut.action) shortcut.action(*this);
    }
}

//绘制指定菜单里的快捷键条目
void EditorSystem::DrawShortcutMenuItems(const char* menu)
{
    for (const EditorShortcut& shortcut : GetEditorShortcuts())
    {
        if (!shortcut.menu || !shortcut.menuLabel || std::strcmp(shortcut.menu, menu) != 0) continue;

        if (ImGui::MenuItem(shortcut.menuLabel, shortcut.display) && shortcut.action) shortcut.action(*this);
    }
}

//获取场景标题：<场景名> * - <项目名>，未保存时带星号
std::string EditorSystem::GetSceneTitle() const
{
    if (!project.HasProject()) return std::string();

    std::string title = project.GetProjectName();
    std::string scene = project.GetCurrentWorldKey();
    if (!scene.empty())
    {
        //只取文件名，标题栏不显示内容根内的目录
        usize separator = scene.find_last_of("/\\");
        if (separator != std::string::npos) scene.erase(0, separator + 1);
        title = title.empty() ? scene : scene + " - " + title;
    }

    if (app.GetWorld().IsDirty()) title += " *";
    return title;
}

//按场景标题更新主窗口标题
void EditorSystem::UpdateWindowTitle()
{
    std::string sceneTitle = GetSceneTitle();
    std::string title = sceneTitle.empty() ? std::string("Orbeden Editor") : sceneTitle + " - Orbeden Editor";
    //标题没变就不碰窗口，避免每帧写一次系统标题
    if (title == windowTitle) return;

    windowTitle = title;
    if (GLFWwindow* window = EditorGUI::GetMainGlfwWindow()) glfwSetWindowTitle(window, windowTitle.c_str());
}

void EditorSystem::DrawMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("Project"))
    {
        if (project.HasProject())
        {
            ImGui::TextUnformatted(project.GetProjectName().c_str());
            ImGui::Separator();
        }

        if (!project.HasProject() || playMode.IsPlaying())
        {
            ImGui::BeginDisabled();
        }

        DrawShortcutMenuItems("Project");

        if (!project.HasProject() || playMode.IsPlaying())
        {
            ImGui::EndDisabled();
        }

        if (ImGui::Selectable("New...", false))
        {
            OpenNewProjectDialog();
        }

        if (ImGui::Selectable("Load...", false))
        {
            OpenProjectDialog();
        }

        if (!projectStatus.empty())
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", projectStatus.c_str());
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if (playMode.IsPlaying())
        {
            ImGui::BeginDisabled();
        }
        DrawShortcutMenuItems("Edit");
        if (playMode.IsPlaying())
        {
            ImGui::EndDisabled();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Views"))
    {
        panelManager.DrawViewsMenu();
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

//绘制顶部播放工具栏
void EditorSystem::DrawPlayToolbar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return;

    constexpr float32 toolbarHeight = 34.0f;
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove;

    bool open = ImGui::BeginViewportSideBar("##EditorPlayToolbar", viewport, ImGuiDir_Up, toolbarHeight, flags);
    if (open)
    {
        float32 buttonWidth = 36.0f;
        float32 spacing = ImGui::GetStyle().ItemSpacing.x;
        float32 totalWidth = buttonWidth * 3.0f + spacing * 2.0f;
        ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowWidth() - totalWidth) * 0.5f));
        ImGui::SetCursorPosY(5.0f);

        //播放按钮。
        bool playDisabled = playMode.IsPlaying();
        if (playDisabled)
        {
            ImGui::BeginDisabled();
        }
        if (DrawToolbarIconButton("##play_button", ToolbarIcon::Play, ImVec2(buttonWidth, 24.0f)))
        {
            RequestPlay();
        }
        if (playDisabled)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        //暂停按钮。
        bool pauseDisabled = !playMode.IsPlaying();
        bool pauseHighlighted = app.IsPaused();
        if (pauseDisabled)
        {
            ImGui::BeginDisabled();
        }
        if (pauseHighlighted)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (DrawToolbarIconButton("##pause_button", ToolbarIcon::Pause, ImVec2(buttonWidth, 24.0f)))
        {
            app.SetPaused(!app.IsPaused());
        }
        if (pauseHighlighted)
        {
            ImGui::PopStyleColor();
        }
        if (pauseDisabled)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        //停止按钮。
        bool stopDisabled = !playMode.IsPlaying();
        if (stopDisabled)
        {
            ImGui::BeginDisabled();
        }
        if (DrawToolbarIconButton("##stop_button", ToolbarIcon::Stop, ImVec2(buttonWidth, 24.0f)))
        {
            RequestStop();
        }
        if (stopDisabled)
        {
            ImGui::EndDisabled();
        }
    }

    ImGui::End();
}

//绘制底部状态栏。边栏与工作区收缩在这里，栏内显示什么由托管侧决定
void EditorSystem::DrawStatusBar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return;

    constexpr float32 statusBarHeight = 26.0f;
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove;

    if (ImGui::BeginViewportSideBar("##EditorStatusBar", viewport, ImGuiDir_Down, statusBarHeight, flags))
    {
        managedBridge.DrawStatusBar();
    }

    ImGui::End();
}

void EditorSystem::DrawProjectDialog()
{
    if (openProjectDialog)
    {
        ImGui::OpenPopup("Load Project Folder");
        openProjectDialog = false;
    }

    ImGui::SetNextWindowSize(ImVec2(640.0f, 460.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Load Project Folder", nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    ImGui::InputText("Path", pathBuffer, sizeof(pathBuffer));
    dialogDirectory = pathBuffer;

    if (ImGui::Button("Up"))
    {
        std::filesystem::path parent = Utf8Path::FromUtf8(dialogDirectory).parent_path();
        if (!parent.empty())
        {
            SetDialogDirectory(ToCleanPath(parent));
        }
    }

    ImGui::Separator();

    ImGui::BeginChild("ProjectDirectoryList", ImVec2(0.0f, 300.0f), true);
    if (std::filesystem::is_directory(dialogDirectory))
    {
        for (const std::string& child : GetChildDirectories(dialogDirectory))
        {
            if (ImGui::Selectable(child.c_str()))
            {
                SetDialogDirectory(ToCleanPath(Utf8Path::FromUtf8(dialogDirectory) / Utf8Path::FromUtf8(child)));
            }
        }
    }
    else
    {
        ImGui::TextUnformatted("Directory does not exist.");
    }
    ImGui::EndChild();

    if (!dialogError.empty())
    {
        ImGui::TextWrapped("%s", dialogError.c_str());
    }

    if (ImGui::Button("Load"))
    {
        //版本闸门必须在任何副作用之前：RequestStop 和 SaveEditorLayout 都会动当前项目的状态。
        ProjectVersionProbe probe;
        std::string probeError;
        if (!EditorProject::ProbeProjectFolder(dialogDirectory, probe, probeError))
        {
            dialogError = probeError;
            projectStatus = probeError;
        }
        else if (probe.status == ProjectVersionStatus::Newer)
        {
            dialogError = "This project was created by a newer version of Orbeden (project version "
                + std::to_string(probe.storedVersion) + ", this Editor supports "
                + std::to_string(OrbedenProjectVersion) + "). Update Orbeden before opening it.";
            projectStatus = dialogError;
        }
        else if (probe.status == ProjectVersionStatus::Outdated)
        {
            pendingUpgrade = probe;
            upgradeError.clear();
            upgradeProjectDialog = true;
            ImGui::CloseCurrentPopup();
        }
        else
        {
            LoadProjectFromFolder(dialogDirectory);
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        dialogError.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorSystem::DrawNewProjectDialog()
{
    if (newProjectDialog)
    {
        ImGui::OpenPopup("New Project");
        newProjectDialog = false;
    }

    ImGui::SetNextWindowSize(ImVec2(640.0f, 500.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    ImGui::InputText("Parent Path", pathBuffer, sizeof(pathBuffer));
    dialogDirectory = pathBuffer;
    ImGui::InputText("Project Name", newProjectNameBuffer, sizeof(newProjectNameBuffer));

    if (ImGui::Button("Up"))
    {
        std::filesystem::path parent = Utf8Path::FromUtf8(dialogDirectory).parent_path();
        if (!parent.empty())
        {
            SetDialogDirectory(ToCleanPath(parent));
        }
    }

    ImGui::Separator();

    ImGui::BeginChild("NewProjectDirectoryList", ImVec2(0.0f, 300.0f), true);
    if (std::filesystem::is_directory(dialogDirectory))
    {
        for (const std::string& child : GetChildDirectories(dialogDirectory))
        {
            if (ImGui::Selectable(child.c_str()))
            {
                SetDialogDirectory(ToCleanPath(Utf8Path::FromUtf8(dialogDirectory) / Utf8Path::FromUtf8(child)));
            }
        }
    }
    else
    {
        ImGui::TextUnformatted("Parent directory does not exist.");
    }
    ImGui::EndChild();

    if (!dialogError.empty())
    {
        ImGui::TextWrapped("%s", dialogError.c_str());
    }

    if (ImGui::Button("Create"))
    {
        std::string runtimeDllPath = FindRuntimeCSharpDll();
        if (runtimeDllPath.empty())
        {
            dialogError = "OrbedenCore.CSharp.dll was not found. Build OrbedenCore.vcxproj first.";
            projectStatus = dialogError;
        }
        else
        {
            RequestStop();
            SaveEditorLayout();

            std::string projectRoot;
            std::string error;
            std::string templateDirectory = GetProjectTemplateDirectory();
            if (templateDirectory.empty())
            {
                error = "Project template directory was not found next to the Editor executable. Rebuild OrbedenEditor.";
            }
            else
            {
                NewProjectGenerator::CreateProject(dialogDirectory, newProjectNameBuffer, runtimeDllPath, templateDirectory, projectRoot, error);
            }

            if (error.empty() && project.LoadProjectFolder(projectRoot))
            {
                FinishProjectLoad("Created project", "created");
                ImGui::CloseCurrentPopup();
            }
            else
            {
                dialogError = !error.empty() ? error : project.GetLastError();
                projectStatus = dialogError;
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        dialogError.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorSystem::SetDialogDirectory(const std::string& path)
{
    dialogDirectory = ToCleanPath(Utf8Path::FromUtf8(path));
    CopyToBuffer(pathBuffer, sizeof(pathBuffer), dialogDirectory);
}

//定位模板根目录：优先 exe 旁的分发副本，回退源码树开发环境。
//模板根下分为脚手架（Project/）与示例（Examples/）两部分。
std::string EditorSystem::GetProjectTemplateDirectory() const
{
    std::filesystem::path executableDirectory = ExecutablePath::GetDirectory(executablePath);
    const std::array<std::filesystem::path, 2> candidates =
    {
        executableDirectory / "Templates",
        executableDirectory.parent_path().parent_path() / "Templates",
    };
    for (const std::filesystem::path& candidate : candidates)
    {
        if (std::filesystem::is_directory(candidate / "Project")) return ToCleanPath(candidate);
    }

    return std::string();
}

std::string EditorSystem::GetRepositoryRoot() const
{
    return FindRepositoryRoot();
}

//写回目标只能是源码树里的模板：exe 旁那份是构建产物，下次构建就被刷掉。
std::string EditorSystem::GetSourceTemplateRoot() const
{
    std::string repositoryRoot = FindRepositoryRoot();
    if (repositoryRoot.empty()) return std::string();

    std::filesystem::path templates = Utf8Path::FromUtf8(repositoryRoot) / "OrbedenEditor" / "Templates";
    if (!std::filesystem::is_directory(templates / "Project")) return std::string();

    return ToCleanPath(templates);
}
