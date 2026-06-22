#pragma once

#include "Application.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorProject.h"
#include "Editor/EnsViewPanel.h"
#include "Editor/ManagedEditorOverlay.h"
#include "Editor/PanelManager.h"
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
    bool openProjectDialog = false;
    bool autoLoadAttempted = false;
    bool previousInputEnabled = true;
    EditorSelection selection;
    PanelManager panelManager;
    ProjectPanel projectPanel;
    EnsViewPanel ensViewPanel;
    ScriptSystem scriptSystem;
    ManagedEditorOverlay managedOverlay;
    EnsId editorCameraEns;
    float32 cameraYaw = 35.0f;
    float32 cameraPitch = -22.0f;
    float32 cameraMoveSpeed = 5.0f;
    bool cameraMouseDragging = false;
    int32 cameraMouseMode = 0;
    double previousMouseX = 0.0;
    double previousMouseY = 0.0;

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

    //请求保存当前场景
    void RequestSaveCurrentWorld();

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

    //获取当前World
    World& GetWorld();

    //获取当前World
    const World& GetWorld() const;

    //获取编辑器选择状态
    EditorSelection& GetSelection();

    //获取编辑器选择状态
    const EditorSelection& GetSelection() const;

private:
    //注册内置编辑器面板
    void RegisterBuiltInPanels();

    //确保编辑器观察相机存在
    void EnsureEditorCamera(World& world);

    //使用 GLFW 输入更新编辑器观察相机
    void UpdateEditorCamera(World& world, float deltaTime);

    //保存当前项目启动场景
    void SaveCurrentWorld();

    //Debug 构建自动加载 ExampleProject
    void TryAutoLoadExampleProject();

    //打开项目选择弹窗
    void OpenProjectDialog();

    //绘制顶部菜单栏
    void DrawMainMenuBar();

    //绘制托管 SceneView Gizmos
    void DrawManagedSceneGizmos();

    //绘制项目文件夹选择弹窗
    void DrawProjectDialog();

    //设置路径输入缓存
    void SetDialogDirectory(const std::string& path);
};
