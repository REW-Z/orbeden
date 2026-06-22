#include "Editor/EditorSystem.h"

#include "Log/Log.h"
#include "Platform/GlfwWindow.h"
#include "Platform/InputManager.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Object/Camera.h"
#include "Runtime/Ens.h"
#include "Runtime/ContentContext.h"
#include "Runtime/Object/SpaceComponent.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <imgui.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace
{
    constexpr float32 Pi = 3.14159265358979323846f;
    constexpr const char* EditorCameraId = "world://editor/camera";

    std::string NormalizePath(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    std::filesystem::path GetExecutableDirectory(const std::string& executablePath)
    {
        if (executablePath.empty()) return std::filesystem::current_path();

        std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(executablePath));
        return path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
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
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
        {
            if (error) break;
            if (!entry.is_directory()) continue;

            result.push_back(entry.path().filename().string());
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    quaternion Mul(const quaternion& a, const quaternion& b)
    {
        return
        {
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        };
    }

    quaternion MakeYawPitchRotation(float32 yawDegrees, float32 pitchDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        float32 pitch = pitchDegrees * Pi / 180.0f;
        quaternion yawRotation = { 0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f) };
        quaternion pitchRotation = { std::sin(pitch * 0.5f), 0.0f, 0.0f, std::cos(pitch * 0.5f) };
        return Mul(yawRotation, pitchRotation);
    }

    vector3 ForwardFromYawPitch(float32 yawDegrees, float32 pitchDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        float32 pitch = pitchDegrees * Pi / 180.0f;
        float32 cosPitch = std::cos(pitch);
        return RenderMath::Normalize({ -std::sin(yaw) * cosPitch, std::sin(pitch), -std::cos(yaw) * cosPitch });
    }

    vector3 RightFromYaw(float32 yawDegrees)
    {
        float32 yaw = yawDegrees * Pi / 180.0f;
        return RenderMath::Normalize({ std::cos(yaw), 0.0f, -std::sin(yaw) });
    }

    vector3 Add(const vector3& a, const vector3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    vector3 Scale(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    vector3 UpFromYawPitch(float32 yawDegrees, float32 pitchDegrees)
    {
        vector3 right = RightFromYaw(yawDegrees);
        vector3 forward = ForwardFromYawPitch(yawDegrees, pitchDegrees);
        return RenderMath::Normalize(RenderMath::Cross(right, forward));
    }

    GLFWwindow* GetGlfwWindow(Application& app)
    {
        GlfwWindow* glfwWindow = dynamic_cast<GlfwWindow*>(app.GetWindow());
        return glfwWindow ? glfwWindow->GetGlfwWindow() : nullptr;
    }

    bool IsMouseDown(GLFWwindow* window, int button)
    {
        return window && glfwGetMouseButton(window, button) == GLFW_PRESS;
    }

    bool ImGuiCapturesMouse()
    {
        return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
    }
}

EditorSystem::EditorSystem(Application& application, const char* startupExecutablePath)
    : app(application)
    , project(application)
    , executablePath(startupExecutablePath ? startupExecutablePath : "")
    , projectPanel(*this)
    , ensViewPanel(*this)
{
    previousInputEnabled = InputManager::IsEnabled();
    InputManager::SetEnabled(false);
    SetDialogDirectory(NormalizePath(std::filesystem::current_path()));
    RegisterBuiltInPanels();

    std::filesystem::path executableDirectory = GetExecutableDirectory(executablePath);
    std::filesystem::path managedDirectory = executableDirectory / "Managed";
    ScriptSystemConfig scriptConfig;
    scriptConfig.runtimeConfigPath = NormalizePath(executableDirectory / "OrbedenCore.runtimeconfig.json");
    scriptConfig.managedDirectory = NormalizePath(managedDirectory);
    scriptConfig.runtimeAssemblyPath = NormalizePath(managedDirectory / "Orbeden.Runtime.dll");
    scriptConfig.componentAssemblyPath = scriptConfig.runtimeAssemblyPath;
    if (scriptSystem.Initialize(scriptConfig))
    {
        managedOverlay.Initialize(scriptSystem, executablePath);
    }
}

EditorSystem::~EditorSystem()
{
    managedOverlay.Shutdown();
    scriptSystem.Shutdown();
    InputManager::SetEnabled(previousInputEnabled);

    if (RenderSystem* renderSystem = app.GetRenderSystem())
    {
        renderSystem->SetRenderOverlay(nullptr);
    }
}

void EditorSystem::Update(World& world, float deltaTime)
{
    TryAutoLoadExampleProject();
    UpdateEditorCamera(world, deltaTime);
}

void EditorSystem::Render(World& world, float deltaTime)
{
    (void)world;
    (void)deltaTime;

    if (RenderSystem* renderSystem = app.GetRenderSystem())
    {
        renderSystem->SetRenderOverlay(this);
        renderSystem->SetFpsLabelVisible(false);
    }
}

void EditorSystem::DrawOverlay()
{
    DrawMainMenuBar();
    DrawManagedSceneGizmos();
    DrawProjectDialog();
    panelManager.DrawPanels();
    managedOverlay.DrawPanels();
}

void EditorSystem::RequestOpenProjectDialog()
{
    OpenProjectDialog();
}

void EditorSystem::RequestSaveCurrentWorld()
{
    SaveCurrentWorld();
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

std::string EditorSystem::GetProjectScriptRootPath() const
{
    return project.GetScriptRootPath();
}

std::string EditorSystem::GetProjectManagedRootPath() const
{
    return project.GetManagedRootPath();
}

std::string EditorSystem::GetStartupWorldPath() const
{
    return project.GetStartupWorldPath();
}

const std::string& EditorSystem::GetProjectStatusText() const
{
    return projectStatus;
}

World& EditorSystem::GetWorld()
{
    return app.GetWorld();
}

const World& EditorSystem::GetWorld() const
{
    return app.GetWorld();
}

EditorSelection& EditorSystem::GetSelection()
{
    return selection;
}

const EditorSelection& EditorSystem::GetSelection() const
{
    return selection;
}

void EditorSystem::RegisterBuiltInPanels()
{
    PanelInfo projectInfo;
    projectInfo.id = projectPanel.GetPanelId();
    projectInfo.title = projectPanel.GetPanelTitle();
    projectInfo.defaultVisible = true;
    projectInfo.defaultSize = { 360.0f, 180.0f };
    panelManager.RegisterPanel(projectInfo, &projectPanel);

    PanelInfo ensViewInfo;
    ensViewInfo.id = ensViewPanel.GetPanelId();
    ensViewInfo.title = ensViewPanel.GetPanelTitle();
    ensViewInfo.defaultVisible = true;
    ensViewInfo.defaultSize = { 320.0f, 420.0f };
    panelManager.RegisterPanel(ensViewInfo, &ensViewPanel);
}

void EditorSystem::EnsureEditorCamera(World& world)
{
    Ens* cameraEns = world.GetEns(editorCameraEns);
    if (!cameraEns)
    {
        cameraEns = world.FindEns(StringId(EditorCameraId));
    }
    if (!cameraEns)
    {
        cameraEns = world.CreateEnsWithStableId(EditorCameraId, "EditorCamera");
        if (cameraEns)
        {
            if (SpaceComponent* space = cameraEns->Space())
            {
                space->localPosition = { 5.0f, 3.2f, 7.0f };
                space->localScale = { 1.0f, 1.0f, 1.0f };
            }
        }
    }
    if (!cameraEns) return;

    Camera* camera = cameraEns->GetComponent<Camera>();
    if (!camera)
    {
        camera = cameraEns->AddComponent<Camera>();
    }
    if (camera)
    {
        camera->enabled = true;
        camera->fieldOfView = 60.0f;
        camera->nearPlane = 0.1f;
        camera->farPlane = 1000.0f;
        camera->depth = 10000.0f;
        camera->clearMode = ClearMode::SolidColor;
        camera->clearColor = { 0.62f, 0.78f, 0.96f, 1.0f };
    }

    if (SpaceComponent* space = cameraEns->Space())
    {
        space->localRotation = MakeYawPitchRotation(cameraYaw, cameraPitch);
    }
    editorCameraEns = cameraEns->GetId();
}

void EditorSystem::UpdateEditorCamera(World& world, float deltaTime)
{
    (void)deltaTime;
    EnsureEditorCamera(world);

    SpaceComponent* space = world.GetSpaceComponent(editorCameraEns);
    GLFWwindow* window = GetGlfwWindow(app);
    if (!space || !window) return;

    if (!ImGuiCapturesMouse())
    {
        int32 mode = 0;
        if (IsMouseDown(window, GLFW_MOUSE_BUTTON_MIDDLE))
        {
            mode = 2;
        }
        else if (IsMouseDown(window, GLFW_MOUSE_BUTTON_RIGHT))
        {
            mode = 3;
        }
        else if (IsMouseDown(window, GLFW_MOUSE_BUTTON_LEFT))
        {
            mode = 1;
        }

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        if (mode != 0)
        {
            if (cameraMouseDragging && cameraMouseMode == mode)
            {
                float32 deltaX = static_cast<float32>(mouseX - previousMouseX);
                float32 deltaY = static_cast<float32>(mouseY - previousMouseY);
                if (mode == 1)
                {
                    cameraYaw -= deltaX * 0.12f;
                    cameraPitch -= deltaY * 0.12f;
                    cameraPitch = std::clamp(cameraPitch, -82.0f, 82.0f);
                }
                else if (mode == 2)
                {
                    constexpr float32 panScale = 0.01f;
                    vector3 right = RightFromYaw(cameraYaw);
                    vector3 up = UpFromYawPitch(cameraYaw, cameraPitch);
                    vector3 pan = Add(Scale(right, -deltaX * panScale), Scale(up, deltaY * panScale));
                    space->localPosition = Add(space->localPosition, pan);
                }
                else if (mode == 3)
                {
                    float32 dollyScale = cameraMoveSpeed * 0.01f;
                    vector3 forward = ForwardFromYawPitch(cameraYaw, cameraPitch);
                    space->localPosition = Add(space->localPosition, Scale(forward, -deltaY * dollyScale));
                }
            }

            cameraMouseDragging = true;
            cameraMouseMode = mode;
            previousMouseX = mouseX;
            previousMouseY = mouseY;
        }
        else
        {
            cameraMouseDragging = false;
            cameraMouseMode = 0;
        }
    }
    else
    {
        cameraMouseDragging = false;
        cameraMouseMode = 0;
    }

    space->localRotation = MakeYawPitchRotation(cameraYaw, cameraPitch);
}

void EditorSystem::SaveCurrentWorld()
{
    if (!project.HasProject())
    {
        projectStatus = "No project is open.";
        Log::Warning(projectStatus.c_str());
        return;
    }

    World& world = app.GetWorld();
    Ens* cameraEns = world.GetEns(editorCameraEns);
    if (!cameraEns)
    {
        cameraEns = world.FindEns(StringId(EditorCameraId));
    }

    bool hadEditorCamera = cameraEns != nullptr;
    vector3 editorCameraPosition;
    quaternion editorCameraRotation;
    if (hadEditorCamera)
    {
        if (SpaceComponent* space = cameraEns->Space())
        {
            editorCameraPosition = space->localPosition;
            editorCameraRotation = space->localRotation;
        }

        cameraEns->Destroy();
        editorCameraEns = EnsId();
    }

    bool saved = project.SaveStartupWorld();
    projectStatus = saved ? ("Saved: " + project.GetStartupWorldPath()) : project.GetLastError();

    EnsureEditorCamera(world);
    if (hadEditorCamera)
    {
        if (SpaceComponent* space = world.GetSpaceComponent(editorCameraEns))
        {
            space->localPosition = editorCameraPosition;
            space->localRotation = editorCameraRotation;
        }
    }

    cameraMouseDragging = false;
    cameraMouseMode = 0;
}

void EditorSystem::TryAutoLoadExampleProject()
{
    if (autoLoadAttempted) return;
    autoLoadAttempted = true;

#if !defined(NDEBUG)
    std::string exampleProject = ContentContext::FindProjectRoot("ExampleProject", executablePath);
    if (exampleProject.empty())
    {
        Log::Warning("Editor Debug auto-load skipped: ExampleProject was not found.");
        return;
    }

    project.LoadProjectFolder(exampleProject);
    if (project.HasProject())
    {
        SetDialogDirectory(project.GetProjectRoot());
        projectStatus = "Loaded: " + project.GetProjectRoot();
        selection.Clear();
        EnsureEditorCamera(app.GetWorld());
    }
#endif
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

        if (!project.HasProject())
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Selectable("Save", false))
        {
            SaveCurrentWorld();
        }

        if (!project.HasProject())
        {
            ImGui::EndDisabled();
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

    if (ImGui::BeginMenu("Views"))
    {
        panelManager.DrawViewsMenu();
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void EditorSystem::DrawManagedSceneGizmos()
{
    World& world = app.GetWorld();
    SpaceComponent* space = world.GetSpaceComponent(editorCameraEns);
    Camera* camera = nullptr;
    if (Ens* cameraEns = world.GetEns(editorCameraEns))
    {
        camera = cameraEns->GetComponent<Camera>();
    }
    if (!space || !camera || !camera->enabled) return;

    // 使用当前 EditorCamera 计算 SceneView 的 VP 矩阵。
    IWindow* window = app.GetWindow();
    int32 width = window ? window->GetFramebufferWidth() : 0;
    int32 height = window ? window->GetFramebufferHeight() : 0;
    float32 aspect = height > 0 ? static_cast<float32>(width) / static_cast<float32>(height) : 1.0f;
    matrix4x4 worldMatrix = RenderMath::TRS(space->localPosition, space->localRotation, space->localScale);
    matrix4x4 viewMatrix = RenderMath::Inverse(worldMatrix);
    matrix4x4 projectionMatrix = RenderMath::Perspective(camera->fieldOfView, aspect, camera->nearPlane, camera->farPlane);
    matrix4x4 viewProjection = RenderMath::Mul(projectionMatrix, viewMatrix);

    managedOverlay.DrawSceneGizmos(viewProjection, width, height);
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
        std::filesystem::path parent = std::filesystem::path(dialogDirectory).parent_path();
        if (!parent.empty())
        {
            SetDialogDirectory(NormalizePath(parent));
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
                SetDialogDirectory(NormalizePath(std::filesystem::path(dialogDirectory) / child));
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
        if (project.LoadProjectFolder(dialogDirectory))
        {
            dialogError.clear();
            SetDialogDirectory(project.GetProjectRoot());
            projectStatus = "Loaded: " + project.GetProjectRoot();
            selection.Clear();
            EnsureEditorCamera(app.GetWorld());
            ImGui::CloseCurrentPopup();
        }
        else
        {
            dialogError = project.GetLastError();
            projectStatus = dialogError;
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
    dialogDirectory = NormalizePath(std::filesystem::path(path));
    CopyToBuffer(pathBuffer, sizeof(pathBuffer), dialogDirectory);
}
