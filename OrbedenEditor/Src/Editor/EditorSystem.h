#pragma once

#include "Application.h"
#include "Editor/EditorProject.h"
#include "Rendering/RenderSystem.h"
#include "Runtime/EnsId.h"

#include <string>

//编辑器主系统，负责项目菜单和编辑器 ImGui 覆盖层。
class EditorSystem : public IEngineSystem, public IImGuiOverlay
{
private:
    Application& app;
    EditorProject project;
    std::string executablePath;
    std::string dialogDirectory;
    std::string dialogError;
    std::string saveStatus;
    char pathBuffer[1024] = {};
    bool openProjectDialog = false;
    bool autoLoadAttempted = false;
    bool previousInputEnabled = true;
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

    //接入渲染系统的 ImGui 覆盖层
    void Render(World& world, float deltaTime) override;

    //绘制编辑器 ImGui
    void DrawImGui() override;

private:
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

    //绘制项目文件夹选择弹窗
    void DrawProjectDialog();

    //设置路径输入缓存
    void SetDialogDirectory(const std::string& path);
};
