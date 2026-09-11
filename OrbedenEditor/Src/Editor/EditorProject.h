#pragma once

#include "Defines/Version.h"
#include "Editor/EditorLayoutState.h"

#include <string>
#include <utility>

class Application;

//项目存档版本与当前 SDK 的比对结果。
enum class ProjectVersionStatus
{
    Current,     //与当前 SDK 一致，可以直接加载
    Outdated,    //落后于当前 SDK，需要升级
    Newer,       //由更新版本的 Orbeden 创建，拒绝加载
};

//只读探测得到的项目版本信息。
//同时带上升级需要的路径：被拦下的项目未必是当前已打开的那个，
//不能靠 EditorProject 的现有成员推导。
struct ProjectVersionProbe
{
    ProjectVersionStatus status = ProjectVersionStatus::Current;
    uint32 storedVersion = 0;
    std::string projectFilePath;
    std::string projectRoot;
    std::string projectName;
    //升级会重铺 .oeproj，启动场景必须在删除前先带出来。
    std::string startupWorld;
};

//编辑器当前打开的项目，负责读取 .oeproj 并加载启动 World。
class EditorProject
{
private:
    Application& app;
    std::string projectRoot;
    std::string projectName;
    std::string startupWorld;
    //当前正在编辑的场景，相对内容根。打开项目时等于 startupWorld，之后可以切换到别的场景。
    std::string currentWorld;
    std::string projectFilePath;
    EditorLayoutState editorLayout;
    std::string lastError;
    bool worldLoaded = false;

public:
    explicit EditorProject(Application& application);

    //只读探测项目版本；不改变任何编辑器状态，加载前的版本闸门使用。
    static bool ProbeProjectFile(const std::string& projectFile, ProjectVersionProbe& outProbe, std::string& outError);
    static bool ProbeProjectFolder(const std::string& folder, ProjectVersionProbe& outProbe, std::string& outError);

    //写入项目版本号；成功写入代表一次升级完成，必须在其它步骤全部成功后调用。
    static bool WriteProjectVersion(const std::string& projectFile, uint32 version, std::string& outError);

    //一次读改写里增删若干根标签属性；升级用它同时更新版本号并清掉废弃属性，避免重复写文件。
    static bool UpdateProjectRootAttributes(const std::string& projectFile,
        const List<std::pair<std::string, std::string>>& attributes,
        const List<std::string>& removedAttributes,
        std::string& outError);

    //从项目目录中查找并加载 .oeproj
    bool LoadProjectFolder(const std::string& folder);

    //读取指定 .oeproj 并加载启动 World
    bool LoadProjectFile(const std::string& projectFile);

    //保存当前场景到磁盘
    bool SaveWorld();

    //重新读取当前场景
    bool ReloadWorld();

    //切换到项目内的另一个场景并加载。路径以内容根为基准，场景放在任何目录都能打开。
    bool OpenWorld(const std::string& relativePath);

    //判断当前场景是否已经完整加载到内存。
    bool IsWorldLoaded() const;

    //标记内存 World 已清空，保存必须等待磁盘重载。
    void MarkWorldPendingReload();

    //保存编辑器布局状态到项目文件
    bool SaveEditorLayout(const EditorLayoutState& layout);

    //获取编辑器布局状态
    const EditorLayoutState& GetEditorLayout() const;

    //获取当前项目根目录
    const std::string& GetProjectRoot() const;

    //获取当前项目名
    const std::string& GetProjectName() const;

    //获取内容根目录：资源、场景与脚本的根，内部结构完全自由。
    std::string GetContentRootPath() const;

    //获取 C# 开发构建输出目录
    std::string GetManagedRootPath() const;

    //获取原生模块产物的目录
    std::string GetNativeBuildPath() const;

    //获取当前场景完整路径
    std::string GetWorldPath() const;

    //获取当前场景相对内容根的 Key
    const std::string& GetCurrentWorldKey() const;

    //获取项目文件完整路径
    const std::string& GetProjectFilePath() const;

    //获取最近一次错误
    const std::string& GetLastError() const;

    //判断是否已经打开项目
    bool HasProject() const;
};
