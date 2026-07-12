#pragma once

#include "Application.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorSelectionOutline.h"
#include "Editor/EditorProject.h"
#include "Editor/EnsViewPanel.h"
#include "Editor/ManagedEditorOverlay.h"
#include "Editor/PanelManager.h"
#include "Editor/EditorPlayMode.h"
#include "Editor/InspectorPanel.h"
#include "Editor/ManagedEditorPanel.h"
#include "Editor/ProjectPanel.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/EnsId.h"

#include <string>

//编辑器主系统，负责项目菜单和编辑器渲染覆盖层。
class EditorSystem : public IEngineSystem, public IRenderOverlay
{
private:
    Application& app;
    EditorProject project;
    std::string executablePath;
    std::string dialogDirectory;
    std::string dialogError;
    std::string projectStatus;
    char pathBuffer[1024] = {};
    char newProjectNameBuffer[128] = {};
    bool openProjectDialog = false;
    bool newProjectDialog = false;
    bool autoLoadAttempted = false;
    bool previousInputEnabled = true;
    EditorSelection selection;
    EditorSelectionOutline selectionOutline;
    PanelManager panelManager;
    ProjectPanel projectPanel;
    EnsViewPanel ensViewPanel;
    InspectorPanel inspectorPanel;
    ManagedEditorPanel managedEditorPanel;
    EditorClrHost clrHost;
    ManagedEditorOverlay managedOverlay;
    EditorPlayMode playMode;
    EditorCameraState editorCameraState;
    int32 selectedPlayerTargetPlatform = 0;
    EnsId editorCameraEns;
    float32 cameraYaw = 35.0f;
    float32 cameraPitch = -22.0f;
    float32 cameraMoveSpeed = 5.0f;
    bool cameraMouseDragging = false;
    int32 cameraMouseMode = 0;
    double previousMouseX = 0.0;
    double previousMouseY = 0.0;
    bool sceneSelectionPressed = false;
    bool sceneSelectionDragged = false;
    bool sceneSelectionCtrl = false;
    vector2 sceneSelectionStart = { 0.0f, 0.0f };

public:
    EditorSystem(Application& application, const char* startupExecutablePath);
    ~EditorSystem() override;

    //每帧更新编辑器状态
    void Update(World& world, float deltaTime) override;

    //接入渲染系统的覆盖层
    void Render(World& world, float deltaTime) override;

    //绘制编辑器覆盖层
    void DrawOverlay() override;

    //请求打开项目选择弹窗
    void RequestOpenProjectDialog();

    //请求打开新建项目弹窗
    void RequestNewProjectDialog();

    //请求保存当前场景
    void RequestSaveCurrentWorld();

    //请求构建当前项目 C# 脚本
    void RequestBuildScripts();

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

    //获取编辑器选择状态
    EditorSelection& GetSelection();

    //获取编辑器选择状态
    const EditorSelection& GetSelection() const;

    //判断是否是编辑器临时 Ens
    bool IsEditorTemporaryEns(EnsId ens) const;

    //绘制托管 Inspector 内容
    void DrawManagedInspectorContent();

    //绘制托管 Editor 面板内容
    void DrawManagedEditorPanelContent();

private:
    //获取当前项目脚本工程路径
    std::string GetProjectScriptProjectPath() const;

    //获取当前项目游戏程序集名
    std::string GetProjectGameAssemblyName() const;

    //获取当前项目 GameModule 类型名
    std::string GetProjectGameModuleTypeName() const;

    //获取当前项目游戏程序集路径
    std::string GetProjectGameAssemblyPath() const;

    //获取当前 world 脚本 sidecar 路径
    std::string GetProjectScriptSidecarPath() const;

    //刷新 Inspector 使用的用户游戏程序集
    bool RefreshInspectorGameAssembly();

    //获取当前选中 Ens 的稳定 ID
    std::string GetSelectedEnsStableId() const;

    //查找仓库根目录
    std::string FindRepositoryRoot() const;

    //查找当前 Editor 可用的 OrbedenCore.CSharp.dll
    std::string FindRuntimeCSharpDll() const;

    //获取 Play/Inspector 需要复制的托管依赖目录
    List<std::string> GetManagedDependencyDirectories() const;

    //运行外部命令
    bool RunCommand(const std::string& command, const char* actionName);

    //注册内置编辑器面板
    void RegisterBuiltInPanels();

    //确保编辑器观察相机存在
    void CreateEditorCameraIfMissing(World& world);

    //记录编辑器观察相机状态
    void CaptureEditorCameraState();

    //应用编辑器观察相机状态
    void ApplyEditorCameraState(const EditorCameraState& state);

    //移除编辑器观察相机
    void RemoveEditorCamera(World& world);

    //使用 GLFW 输入更新编辑器观察相机
    void UpdateEditorCamera(World& world, float deltaTime);

    /// <summary>处理中央工作区的鼠标选择。</summary>
    void HandleSceneSelection(const RenderScene& scene);

    /// <summary>拾取鼠标下距离相机最近的场景对象。</summary>
    EnsId PickSceneEns(const RenderScene& scene, const vector2& screenPosition) const;

    //保存当前项目启动场景
    bool SaveCurrentWorld();

    //保存当前编辑器布局
    void SaveEditorLayout();

    //应用当前项目编辑器布局
    void ApplyEditorLayout();

    //Debug 构建自动加载 ExampleProject
    void TryAutoLoadExampleProject();

    //打开项目选择弹窗
    void OpenProjectDialog();

    //打开新建项目弹窗
    void OpenNewProjectDialog();

    //绘制顶部菜单栏
    void DrawMainMenuBar();

    //绘制顶部播放工具栏
    void DrawPlayToolbar();

    //绘制托管 SceneView Gizmos
    void DrawManagedSceneGizmos();

    //绘制项目文件夹选择弹窗
    void DrawProjectDialog();

    //绘制新建项目弹窗
    void DrawNewProjectDialog();

    //设置路径输入缓存
    void SetDialogDirectory(const std::string& path);
};
