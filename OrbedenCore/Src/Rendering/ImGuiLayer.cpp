#include "Rendering/ImGuiLayer.h"

#include "Log/Log.h"
#include "Platform/GlfwWindow.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

bool ImGuiLayer::Initialize(IWindow* window)
{
    if (initialized) return true;
    if (!window)
    {
        Log::Warning("ImGuiLayer initialize skipped: window is missing.");
        return false;
    }

    if (window->GetGraphicsApi() != WindowGraphicsApi::OpenGL)
    {
        Log::Warning("ImGuiLayer initialize skipped: only OpenGL is supported now.");
        return false;
    }

    GlfwWindow* glfwWindow = dynamic_cast<GlfwWindow*>(window);
    if (!glfwWindow || !glfwWindow->GetGlfwWindow())
    {
        Log::Warning("ImGuiLayer initialize skipped: GLFW window is missing.");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOpenGL(glfwWindow->GetGlfwWindow(), true))
    {
        Log::Warning("ImGuiLayer initialize skipped: GLFW backend initialize failed.");
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 430"))
    {
        Log::Warning("ImGuiLayer initialize skipped: OpenGL3 backend initialize failed.");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    initialized = true;
    return true;
}

void ImGuiLayer::Shutdown()
{
    if (!initialized) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    initialized = false;
}

void ImGuiLayer::BeginFrame()
{
    if (!initialized) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::DrawFpsLabel()
{
    if (!initialized) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 position = ImVec2(viewport->WorkPos.x + 8.0f, viewport->WorkPos.y + 8.0f);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);
    if (ImGui::Begin("DebugFpsLabel", nullptr, flags))
    {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    }
    ImGui::End();
}

void ImGuiLayer::Render()
{
    if (!initialized) return;

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiLayer::IsInitialized() const
{
    return initialized;
}
