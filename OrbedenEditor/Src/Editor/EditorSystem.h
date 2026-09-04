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
    char pathBuffer[1024] = {};
    char newProjectNameBuffer[128] = {};
    bool openProjectDialog = false;
    bool newProjectDialog = false;
    bool previousInputEnabled = true;
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

    //请求打开新建项目弹窗
    void RequestNewProjectDialog();

    //请求保存当前场景
    void RequestSaveCurrentWorld();

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

    //获取当前项目名
    const std::string& GetProjectName() const;

    //获取当前项目根目录
    const std::string& GetProjectRoot() const;

    //获取项目脚本根目录
    std::string GetProjectScriptRootPath() const;

    //获取项目托管输出目录
    std::string GetProjectManagedRootPath() const;

    //获取项目 C++ 代码根目录。
    std::string GetProjectNativeRootPath() const;

    //获取启动场景完整路径
    std::string GetStartupWorldPath() const;

    //获取项目操作状态文本
    const std::string& GetProjectStatusText() const;

    //获取 Player 目标平台数量
    int32 GetPlayerTargetPlatformCount() const;

    //获取当前 Player 目标平台索引
    int32 GetSelectedPlayerTargetPlatformIndex() const;

    //获取 Player 目标平台显示名
    const char* GetPlayerTargetPlatformName(int32 index) const;

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

    //保存当前项目启动场景
    bool SaveCurrentWorld();

    //保存当前编辑器布局
    void SaveEditorLayout();

    //应用当前项目编辑器布局
    void ApplyEditorLayout();

    //打开项目选择弹窗
    void OpenProjectDialog();

    //打开新建项目弹窗
    void OpenNewProjectDialog();

    //绘制顶部菜单栏
    void DrawMainMenuBar();

    //绘制顶部播放工具栏
    void DrawPlayToolbar();

    //绘制项目文件夹选择弹窗
    void DrawProjectDialog();

    //绘制新建项目弹窗
    void DrawNewProjectDialog();

    //设置路径输入缓存
    void SetDialogDirectory(const std::string& path);
};
