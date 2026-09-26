#pragma once

#include "Application.h"
#include "Editor/EditorGUI.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorScene.h"
#include "Editor/ManagedEditorBridge.h"
#include "Editor/NativeGameModule.h"
#include "Editor/PanelManager.h"
#include "Editor/EditorPlayMode.h"
#include "Runtime/EnsId.h"

#include <atomic>
#include <string>

class ManagedPanelAdapter;

//编辑器快捷键的生效范围
enum class EditorShortcutScope
{
    Global,    //任何位置都生效
    SceneView, //鼠标位于场景视口内才生效
    Gizmo,     //手柄正在拖拽时才生效
};

//一条编辑器快捷键；menu 为空表示只参与分发，不出现在菜单里
struct EditorShortcut
{
    const char* menu = nullptr;
    const char* menuLabel = nullptr;
    const char* display = nullptr;
    ImGuiKey key = ImGuiKey_None;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    EditorShortcutScope scope = EditorShortcutScope::Global;
    //执行快捷键动作，函数指针而不是 lambda：整张表是静态常量
    void (*action)(EditorSystem& editor) = nullptr;
};

//编辑器主系统
class EditorSystem
{
private:
    Application& app;
    EditorGUI editorGUI;
    EditorProject project;
    std::string executablePath;
    std::string dialogDirectory;
    std::string dialogError;
    std::string projectStatus;
    std::string pendingProjectFile;
    std::string upgradeError;
    char pathBuffer[1024] = {};
    char newProjectNameBuffer[128] = {};
    bool openProjectDialog = false;
    bool newProjectDialog = false;
    //版本闸门拦下的待升级项目。用户选择"退出"时直接丢弃，编辑器状态不受影响。
    bool upgradeProjectDialog = false;
    ProjectVersionProbe pendingUpgrade;
    bool previousInputEnabled = true;
    std::string windowTitle;
    EditorLayoutState playPanelLayout;
    PanelManager panelManager;
    EditorClrHost clrHost;
    ManagedEditorBridge managedBridge;
    EditorScene editorScene;
    EditorPlayMode playMode;
    NativeGameModule nativeGameModule;
    int32 selectedPlayerTargetPlatform = 0;
    std::atomic_bool repaintRequested = true;
    bool continuousRepaint = false;

public:
    EditorSystem(Application& application, const char* startupExecutablePath);
    ~EditorSystem();

    //每帧更新编辑器状态
    void Update(World& world, float deltaTime);

    //请求编辑器重绘并唤醒事件循环
    void RequestRepaint();

    //获取并清除编辑器重绘请求
    bool TakeRepaintRequest();

    //判断编辑器是否需要连续重绘
    bool NeedsContinuousRepaint() const;

    //绘制 EditorGUI
    void RenderEditorGUI();

    //请求打开项目选择弹窗
    void RequestOpenProjectDialog();

    /// <summary>延迟到帧更新阶段打开指定项目文件，避免在托管面板绘制中切换程序集。</summary>
    void RequestOpenProjectFile(const std::string& path);

    //请求打开新建项目弹窗
    void RequestNewProjectDialog();

    //请求保存当前场景
    void RequestSaveCurrentWorld();

    /// <summary>保存当前编辑状态，失败时阻止模板写回。</summary>
    bool SaveCurrentWorld();

    /// <summary>从磁盘重新构建并加载项目内容，丢弃重置前的场景和缓存。</summary>
    bool ReloadProjectContent();

    //打开项目内的另一个场景。路径以项目根为基准。
    bool OpenWorld(const std::string& relativeKey);

    //请求构建当前项目 C# 脚本
    void RequestBuildScripts();

    //请求构建并热重载当前项目 C++ 模块。
    void RequestBuildNative();

    //请求进入 Play-In-Editor
    void RequestPlay();

    //请求停止 Play-In-Editor
    void RequestStop();

    //请求构建发布版 Player
    void RequestBuildPlayer();

    //判断是否正在 Play-In-Editor
    bool IsPlaying() const;

    //判断是否已经打开项目
    bool HasProject() const;

    //读取编辑项目的 World 配置
    EditorProject& GetProject() { return project; }

    //获取当前项目名
    const std::string& GetProjectName() const;

    //获取当前项目根目录
    const std::string& GetProjectRoot() const;

    //获取项目内容根目录：资源、场景与脚本的根，内部结构完全自由。
    std::string GetProjectContentRootPath() const;

    //获取项目托管输出目录
    std::string GetProjectManagedRootPath() const;

    //获取原生模块产物目录
    std::string GetProjectNativeBuildPath() const;

    //定位新项目模板目录（优先 exe 旁的分发副本，回退源码树）。
    std::string GetProjectTemplateDirectory() const;

    //定位仓库根目录，找不到返回空串。
    std::string GetRepositoryRoot() const;

    //定位源码树里的模板根：示例写回只能落在这里，不能落进 exe 旁那份随构建刷新的分发副本。
    std::string GetSourceTemplateRoot() const;

    //获取启动场景完整路径
    std::string GetWorldPath() const;

    //获取项目操作状态文本
    const std::string& GetProjectStatusText() const;

    //获取 Player 目标平台数量
    int32 GetPlayerTargetPlatformCount() const;

    //获取当前 Player 目标平台索引
    int32 GetSelectedPlayerTargetPlatformIndex() const;

    //获取 Player 目标平台显示名
    const char* GetPlayerTargetPlatformName(int32 index) const;

    //判断 Player 目标平台当前是否可用（未接通的平台在打包界面置灰）
    bool IsPlayerTargetPlatformAvailable(int32 index) const;

    //设置当前 Player 目标平台
    void SetSelectedPlayerTargetPlatformIndex(int32 index);

    //获取当前 Player 目标平台显示名
    const char* GetSelectedPlayerTargetPlatformName() const;

    //获取当前World
    World& GetWorld();

    //获取当前World
    const World& GetWorld() const;

    //获取编辑器背景场景
    EditorScene& GetEditorScene();

    //获取编辑器背景场景
    const EditorScene& GetEditorScene() const;

private:
    friend class ManagedPanelAdapter;

    //获取当前项目脚本工程路径
    std::string GetProjectScriptProjectPath() const;

    //获取当前项目游戏程序集名
    std::string GetProjectGameAssemblyName() const;

    //获取当前项目游戏程序集路径
    std::string GetProjectGameAssemblyPath() const;


    //刷新 Inspector 使用的用户游戏程序集
    bool RefreshInspectorGameAssembly();

    //构建并热重载项目 C++ 模块。
    bool BuildNativeGameModule(bool saveWorldBeforeReload);

    //把内容根内的资源 cook 到 ResourceCache，并重建当前场景
    bool CookPlayerContent(std::string& error);

    //清空包内 Content 后同步 cook 产物，再把 .oeproj 复制到包根
    bool SyncPlayerPackage(const std::string& packageRoot, std::string& error);

    //查找仓库根目录
    std::string FindRepositoryRoot() const;

    //查找当前 Editor 可用的 OrbedenCore.CSharp.dll
    std::string FindRuntimeCSharpDll() const;

    //Debug模式下同步Core C#运行库到当前游戏项目
    bool SyncProjectRuntimeCSharpDll(std::string& outError) const;

    //获取 Play/Inspector 需要复制的托管依赖目录
    List<std::string> GetManagedDependencyDirectories() const;

    //运行外部命令
    bool RunCommand(const std::string& command, const char* actionName);

    //绘制一个托管面板
    void DrawManagedPanel(int32 handle);

    //设置托管面板可见状态
    void SetManagedPanelVisible(int32 handle, bool visible);

    //保存当前编辑器布局
    void SaveEditorLayout();

    //应用当前项目编辑器布局
    void ApplyEditorLayout();

    //项目加载成功后的统一收尾，Load 与 New Project 两条路径共用
    void FinishProjectLoad(const std::string& successLabel, const std::string& pendingNativeLabel);

    //加载一个已通过版本闸门的项目
    void LoadProjectFromFolder(const std::string& folder);

    //对 pendingUpgrade 指向的项目执行升级
    bool RunProjectUpgrade(std::string& outError);

    //绘制项目升级弹窗
    void DrawUpgradeProjectDialog();

    //打开项目选择弹窗
    void OpenProjectDialog();

    //打开新建项目弹窗
    void OpenNewProjectDialog();

    //绘制顶部菜单栏
    void DrawMainMenuBar();

    //获取编辑器快捷键表，菜单显示文本与按键分发共用这一份
    const List<EditorShortcut>& GetEditorShortcuts();

    //分发编辑器快捷键；Play 期间整套失效，按键交给游戏
    void ProcessEditorShortcuts();

    //绘制指定菜单里的快捷键条目
    void DrawShortcutMenuItems(const char* menu);

    //获取场景标题：<场景名> * - <项目名>，未保存时带星号
    std::string GetSceneTitle() const;

    //按场景标题更新主窗口标题
    void UpdateWindowTitle();

    //绘制顶部播放工具栏
    void DrawPlayToolbar();

    //绘制底部状态栏
    void DrawStatusBar();

    //绘制项目文件夹选择弹窗
    void DrawProjectDialog();

    //绘制新建项目弹窗
    void DrawNewProjectDialog();

    //设置路径输入缓存
    void SetDialogDirectory(const std::string& path);
};
