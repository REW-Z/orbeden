#include "Editor/ManagedEditorOverlay.h"

#include "Log/Log.h"
#include "Runtime/Native/OrbedenNativeApi.h"

#include <coreclr_delegates.h>
#include <array>
#include <filesystem>
#include <vector>

namespace
{
    using ManagedInitializeEditorFn = void(CORECLR_DELEGATE_CALLTYPE*)(void*);
    using ManagedDrawEditorFn = void(CORECLR_DELEGATE_CALLTYPE*)();
    using ManagedLoadGameAssemblyFn = void(CORECLR_DELEGATE_CALLTYPE*)(const uint8*, int32, const uint8*, int32);
    using ManagedUnloadGameAssemblyFn = void(CORECLR_DELEGATE_CALLTYPE*)();
    using ManagedDrawInspectorFn = void(CORECLR_DELEGATE_CALLTYPE*)(uint32, uint32, const uint8*, int32);
    using ManagedPublishGameAotFn = uint8(CORECLR_DELEGATE_CALLTYPE*)(
        const uint8*, int32,
        const uint8*, int32,
        const uint8*, int32,
        const uint8*, int32,
        const uint8*, int32,
        uint8*, int32);

    constexpr const char* EditorTypeName = "OrbedenEditor.EditorRuntime, Orbeden.Editor";
    constexpr const char* EditorInitializeMethod = "Initialize";
    constexpr const char* EditorLoadGameAssemblyMethod = "LoadGameAssembly";
    constexpr const char* EditorUnloadGameAssemblyMethod = "UnloadGameAssembly";
    constexpr const char* EditorDrawInspectorMethod = "DrawInspector";
    constexpr const char* EditorDrawInspectorContentMethod = "DrawInspectorContent";
    constexpr const char* EditorDrawPanelsMethod = "DrawPanels";
    constexpr const char* EditorDrawEditorPanelContentMethod = "DrawEditorPanelContent";
    constexpr const char* EditorDrawSceneGizmosMethod = "DrawSceneGizmos";
    constexpr const char* EditorPublishGameAotMethod = "PublishGameAot";

    //传给 Editor C# 的原生函数表。
    struct EditorManagedApi
    {
    public:
        void* nativeApi = nullptr;
        EditorGizmoApi gizmo;
    };

    // 获取可执行文件所在目录。
    std::filesystem::path GetExecutableDirectory(const std::string& executablePath)
    {
        if (executablePath.empty()) return std::filesystem::current_path();

        std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(executablePath));
        return path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    }
}

bool ManagedEditorOverlay::Initialize(EditorClrHost& host, const std::string& executablePath)
{
    if (initialized) return true;
    if (!host.IsInitialized())
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: EditorClrHost is not initialized.");
        return false;
    }

    // 计算托管程序集路径。
    std::filesystem::path executableDirectory = GetExecutableDirectory(executablePath);
    std::filesystem::path managedDirectory = executableDirectory / "Managed";
    std::string editorAssemblyPath = (managedDirectory / "Orbeden.Editor.dll").lexically_normal().generic_string();

    // 绑定 Editor 托管入口。
    clrHost = &host;
    ManagedInitializeEditorFn InitializeEditor = nullptr;
    if (!clrHost->BindFunction(editorAssemblyPath,
        EditorTypeName,
        EditorInitializeMethod,
        reinterpret_cast<void**>(&InitializeEditor)))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor Initialize binding failed.");
        clrHost = nullptr;
        return false;
    }

    if (!clrHost->BindFunction(editorAssemblyPath,
        EditorTypeName,
        EditorDrawPanelsMethod,
        &DrawPanelsFunction))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor DrawPanels binding failed.");
        clrHost = nullptr;
        return false;
    }

    if (!clrHost->BindFunction(editorAssemblyPath,
        EditorTypeName,
        EditorDrawEditorPanelContentMethod,
        &DrawEditorPanelContentFunction))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor DrawEditorPanelContent binding failed.");
        clrHost = nullptr;
        return false;
    }

    if (!clrHost->BindFunction(editorAssemblyPath,
        EditorTypeName,
        EditorLoadGameAssemblyMethod,
        &LoadGameAssemblyFunction))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor LoadGameAssembly binding failed.");
        clrHost = nullptr;
        return false;
    }

    if (!clrHost->BindFunction(editorAssemblyPath,
        EditorTypeName,
        EditorUnloadGameAssemblyMethod,
        &UnloadGameAssemblyFunction))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor UnloadGameAssembly binding failed.");
        clrHost = nullptr;
        return false;
    }

    if (!clrHost->BindFunction(editorAssemblyPath,
        EditorTypeName,
        EditorDrawInspectorMethod,
        &DrawInspectorFunction))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor DrawInspector binding failed.");
        clrHost = nullptr;
        return false;
    }

    if (!clrHost->BindFunction(editorAssemblyPath,
        EditorTypeName,
        EditorDrawInspectorContentMethod,
        &DrawInspectorContentFunction))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor DrawInspectorContent binding failed.");
        clrHost = nullptr;
        return false;
    }

    if (!clrHost->BindFunction(editorAssemblyPath,
        EditorTypeName,
        EditorDrawSceneGizmosMethod,
        &DrawSceneGizmosFunction))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor DrawSceneGizmos binding failed.");
        clrHost = nullptr;
        return false;
    }

    if (!clrHost->BindFunction(editorAssemblyPath,
        EditorTypeName,
        EditorPublishGameAotMethod,
        &PublishGameAotFunction))
    {
        Log::Warning("ManagedEditorOverlay initialize skipped: editor PublishGameAot binding failed.");
        clrHost = nullptr;
        return false;
    }

    // 把 Editor 原生函数表传给 C#。
    OrbedenNativeApi nativeApi = OrbedenNativeApi::Create();
    EditorManagedApi editorApi;
    editorApi.nativeApi = &nativeApi;
    editorApi.gizmo = gizmoBridge.GetApi();
    InitializeEditor(&editorApi);

    initialized = true;
    return true;
}

void ManagedEditorOverlay::Shutdown()
{
    DrawPanelsFunction = nullptr;
    DrawEditorPanelContentFunction = nullptr;
    DrawSceneGizmosFunction = nullptr;
    LoadGameAssemblyFunction = nullptr;
    UnloadGameAssemblyFunction = nullptr;
    DrawInspectorFunction = nullptr;
    DrawInspectorContentFunction = nullptr;
    PublishGameAotFunction = nullptr;
    initialized = false;
    clrHost = nullptr;
}

void ManagedEditorOverlay::DrawPanels()
{
    if (!initialized || DrawPanelsFunction == nullptr) return;

    // 调用 C# Editor panels。
    ManagedDrawEditorFn DrawPanels = reinterpret_cast<ManagedDrawEditorFn>(DrawPanelsFunction);
    DrawPanels();
}

void ManagedEditorOverlay::LoadGameAssembly(const std::string& assemblyPath, const std::string& sidecarPath)
{
    if (!initialized || LoadGameAssemblyFunction == nullptr) return;

    ManagedLoadGameAssemblyFn LoadGameAssembly = reinterpret_cast<ManagedLoadGameAssemblyFn>(LoadGameAssemblyFunction);
    LoadGameAssembly(reinterpret_cast<const uint8*>(assemblyPath.data()),
        static_cast<int32>(assemblyPath.size()),
        reinterpret_cast<const uint8*>(sidecarPath.data()),
        static_cast<int32>(sidecarPath.size()));
}

void ManagedEditorOverlay::UnloadGameAssembly()
{
    if (!initialized || UnloadGameAssemblyFunction == nullptr) return;

    ManagedUnloadGameAssemblyFn UnloadGameAssembly = reinterpret_cast<ManagedUnloadGameAssemblyFn>(UnloadGameAssemblyFunction);
    UnloadGameAssembly();
}

void ManagedEditorOverlay::DrawInspector(EnsId selectedEns, const std::string& stableId)
{
    if (!initialized || DrawInspectorFunction == nullptr) return;

    ManagedDrawInspectorFn DrawInspector = reinterpret_cast<ManagedDrawInspectorFn>(DrawInspectorFunction);
    DrawInspector(selectedEns.id,
        selectedEns.version,
        reinterpret_cast<const uint8*>(stableId.data()),
        static_cast<int32>(stableId.size()));
}

// 绘制 C# Inspector 内容。
void ManagedEditorOverlay::DrawInspectorContent(EnsId selectedEns, const std::string& stableId)
{
    if (!initialized || DrawInspectorContentFunction == nullptr) return;

    ManagedDrawInspectorFn DrawInspectorContent = reinterpret_cast<ManagedDrawInspectorFn>(DrawInspectorContentFunction);
    DrawInspectorContent(selectedEns.id,
        selectedEns.version,
        reinterpret_cast<const uint8*>(stableId.data()),
        static_cast<int32>(stableId.size()));
}

// 绘制 C# Editor 面板内容。
void ManagedEditorOverlay::DrawEditorPanelContent()
{
    if (!initialized || DrawEditorPanelContentFunction == nullptr) return;

    ManagedDrawEditorFn DrawEditorPanelContent = reinterpret_cast<ManagedDrawEditorFn>(DrawEditorPanelContentFunction);
    DrawEditorPanelContent();
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

bool ManagedEditorOverlay::PublishGameAot(const std::string& repositoryRoot,
    const std::string& projectRoot,
    const std::string& scriptProject,
    const std::string& configuration,
    const std::string& targetPlatform,
    std::string& error)
{
    error.clear();
    if (!initialized || PublishGameAotFunction == nullptr)
    {
        error = "Editor managed NativeAOT publisher is not initialized.";
        return false;
    }

    std::array<uint8, 4096> errorBuffer{};
    ManagedPublishGameAotFn PublishGameAot = reinterpret_cast<ManagedPublishGameAotFn>(PublishGameAotFunction);
    uint8 succeeded = PublishGameAot(
        reinterpret_cast<const uint8*>(repositoryRoot.data()), static_cast<int32>(repositoryRoot.size()),
        reinterpret_cast<const uint8*>(projectRoot.data()), static_cast<int32>(projectRoot.size()),
        reinterpret_cast<const uint8*>(scriptProject.data()), static_cast<int32>(scriptProject.size()),
        reinterpret_cast<const uint8*>(configuration.data()), static_cast<int32>(configuration.size()),
        reinterpret_cast<const uint8*>(targetPlatform.data()), static_cast<int32>(targetPlatform.size()),
        errorBuffer.data(), static_cast<int32>(errorBuffer.size()));
    error = reinterpret_cast<const char*>(errorBuffer.data());
    return succeeded != 0;
}

bool ManagedEditorOverlay::IsInitialized() const
{
    return initialized;
}
