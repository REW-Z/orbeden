#include "Editor/ManagedEditorOverlay.h"

#include "Log/Log.h"

#include <coreclr_delegates.h>
#include <filesystem>

namespace
{
    using ManagedInitializeEditorFn = void(CORECLR_DELEGATE_CALLTYPE*)(void*);
    using ManagedDrawEditorFn = void(CORECLR_DELEGATE_CALLTYPE*)();

    constexpr const char* EditorTypeName = "OrbedenEditor.EditorRuntime, Orbeden.Editor";
    constexpr const char* EditorInitializeMethod = "Initialize";
    constexpr const char* EditorDrawPanelsMethod = "DrawPanels";
    constexpr const char* EditorDrawSceneGizmosMethod = "DrawSceneGizmos";

    // 获取可执行文件所在目录。
    std::filesystem::path GetExecutableDirectory(const std::string& executablePath)
    {
        if (executablePath.empty()) return std::filesystem::current_path();

        std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(executablePath));
        return path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    }
}

bool ManagedEditorOverlay::Initialize(ScriptSystem& runtime, const std::string& executablePath)
{
    if (initialized) return true;
    if (!runtime.IsInitialized())
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: ScriptSystem is not initialized.");
        return false;
    }

    // 计算托管程序集路径。
    std::filesystem::path executableDirectory = GetExecutableDirectory(executablePath);
    std::filesystem::path managedDirectory = executableDirectory / "Managed";
    std::string editorAssemblyPath = (managedDirectory / "Orbeden.Editor.dll").lexically_normal().generic_string();

    // 绑定 Editor 托管入口。
    scriptSystem = &runtime;
    ManagedInitializeEditorFn InitializeEditor = nullptr;
    if (!scriptSystem->BindCSharpFunction(editorAssemblyPath,
        EditorTypeName,
        EditorInitializeMethod,
        reinterpret_cast<void**>(&InitializeEditor)))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor Initialize binding failed.");
        scriptSystem = nullptr;
        return false;
    }

    if (!scriptSystem->BindCSharpFunction(editorAssemblyPath,
        EditorTypeName,
        EditorDrawPanelsMethod,
        &DrawPanelsFunction))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor DrawPanels binding failed.");
        scriptSystem = nullptr;
        return false;
    }

    if (!scriptSystem->BindCSharpFunction(editorAssemblyPath,
        EditorTypeName,
        EditorDrawSceneGizmosMethod,
        &DrawSceneGizmosFunction))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor DrawSceneGizmos binding failed.");
        scriptSystem = nullptr;
        return false;
    }

    // 把 Editor Gizmo 原生函数表传给 C#。
    EditorGizmoApi editorGizmoApi = gizmoBridge.GetApi();
    InitializeEditor(&editorGizmoApi);

    initialized = true;
    return true;
}

void ManagedEditorOverlay::Shutdown()
{
    DrawPanelsFunction = nullptr;
    DrawSceneGizmosFunction = nullptr;
    initialized = false;
    scriptSystem = nullptr;
}

void ManagedEditorOverlay::DrawPanels()
{
    if (!initialized || DrawPanelsFunction == nullptr) return;

    // 调用 C# Editor panels。
    ManagedDrawEditorFn DrawPanels = reinterpret_cast<ManagedDrawEditorFn>(DrawPanelsFunction);
    DrawPanels();
}

void ManagedEditorOverlay::DrawSceneGizmos(const matrix4x4& viewProjection, int32 viewportWidth, int32 viewportHeight)
{
    if (!initialized || DrawSceneGizmosFunction == nullptr) return;

    // 设置投影上下文后调用 C# SceneView Gizmos。
    gizmoBridge.BeginFrame(viewProjection, viewportWidth, viewportHeight);
    ManagedDrawEditorFn DrawSceneGizmos = reinterpret_cast<ManagedDrawEditorFn>(DrawSceneGizmosFunction);
    DrawSceneGizmos();
    gizmoBridge.EndFrame();
}

bool ManagedEditorOverlay::IsInitialized() const
{
    return initialized;
}
