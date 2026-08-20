#include "Editor/ManagedEditorBridge.h"

#include "Editor/EditorScene.h"
#include "Editor/EditorSystem.h"
#include "Editor/EditorGUI.h"
#include "Editor/PanelManager.h"
#include "Editor/Panels/ManagedPanelAdapter.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "Log/Log.h"
#include "Runtime/Reflection.h"
#include "Runtime/ResourceManager.h"
#include "Runtime/Native/NativeCall.h"
#include "Runtime/Native/NativeApiAbi.h"
#include "Runtime/Native/OrbedenEngineNativeApi.h"

#include <coreclr_delegates.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <utility>

namespace
{
    using ManagedInitializeEditorFn = uint8(CORECLR_DELEGATE_CALLTYPE*)(void*);
    using ManagedDrawPanelFn = void(CORECLR_DELEGATE_CALLTYPE*)(int32, uint32, uint32, const uint8*, int32);
    using ManagedSetPanelVisibleFn = void(CORECLR_DELEGATE_CALLTYPE*)(int32, uint8);
    using ManagedDrawEditorFn = void(CORECLR_DELEGATE_CALLTYPE*)();
    using ManagedLoadGameAssemblyFn = void(CORECLR_DELEGATE_CALLTYPE*)(const uint8*, int32, const uint8*, int32);
    using ManagedUnloadGameAssemblyFn = void(CORECLR_DELEGATE_CALLTYPE*)();
    using ManagedPublishGameAotFn = uint8(CORECLR_DELEGATE_CALLTYPE*)(
        const uint8*, int32,
        const uint8*, int32,
        const uint8*, int32,
        const uint8*, int32,
        const uint8*, int32,
        uint8*, int32);

    constexpr const char* EditorTypeName = "OrbedenEditor.EditorRuntime, Orbeden.Editor";
    constexpr const char* EditorInitializeMethod = "Initialize";
    constexpr const char* EditorDrawPanelMethod = "DrawPanel";
    constexpr const char* EditorSetPanelVisibleMethod = "SetPanelVisible";
    constexpr const char* EditorLoadGameAssemblyMethod = "LoadGameAssembly";
    constexpr const char* EditorUnloadGameAssemblyMethod = "UnloadGameAssembly";
    constexpr const char* EditorDrawSceneGizmosMethod = "DrawSceneGizmos";
    constexpr const char* EditorPublishGameAotMethod = "PublishGameAot";

    //托管 Panel 注册期间使用的原生上下文。
    struct ManagedPanelRegistrationContext
    {
    public:
        EditorSystem* editor = nullptr;
        PanelManager* panelManager = nullptr;
    };

    #pragma pack(push, 8)

    //传给 Editor C# 的 Panel 注册函数表。
    struct EditorPanelNativeApi
    {
    public:
        void* context = nullptr;
        void* registerPanel = nullptr;
    };

    //传给 Editor C# 的资源函数表。
    struct EditorAssetNativeApi
    {
    public:
        void* context = nullptr;
        void* getResourceRoot = nullptr;
        void* canModifyAssets = nullptr;
        void* remapLiveReferences = nullptr;
    };

    //传给 Editor C# 的应用函数表。
    struct EditorApplicationNativeApi
    {
    public:
        void* context = nullptr;
        void* requestRepaint = nullptr;
    };

    //传给 Editor C# 的原生函数表。
    struct EditorManagedApi
    {
    public:
        void* engineApi = nullptr;
        EditorGuiNativeApi gui;
        EditorApplicationNativeApi application;
        EditorGizmoApi gizmo;
        EditorPanelNativeApi panels;
        EditorAssetNativeApi assets;
    };

    #pragma pack(pop)

    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorPanelNativeApi, 2);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorAssetNativeApi, 4);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorApplicationNativeApi, 2);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorManagedApi, 41);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, engineApi, 0);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, application, 31);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, gizmo, 33);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, panels, 35);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, assets, 37);

    //获取可执行文件所在目录
    std::filesystem::path GetExecutableDirectory(const std::string& executablePath)
    {
        if (executablePath.empty()) return std::filesystem::current_path();

        std::filesystem::path path = std::filesystem::absolute(Utf8Path::FromUtf8(executablePath));
        return path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    }

    //复制 C# 传入的 UTF-8 文本
    std::string ReadUtf8(const uint8* text, int32 length)
    {
        if (!text || length <= 0) return std::string();
        return std::string(reinterpret_cast<const char*>(text), static_cast<usize>(length));
    }

    //复制 UTF-8 文本到托管缓冲区
    int32 CopyUtf8(const std::string& text, uint8* buffer, int32 bufferSize)
    {
        int32 required = static_cast<int32>(text.size());
        if (buffer && bufferSize > 0 && required > 0)
        {
            int32 copyLength = std::min(required, bufferSize);
            std::memcpy(buffer, text.data(), static_cast<usize>(copyLength));
        }

        return required;
    }

    //判断资源 Key 是否命中本次路径映射。
    bool TryMapResourceKey(const std::string& value,
        const std::string& oldKey,
        const std::string& newKey,
        bool prefix,
        std::string& mapped)
    {
        usize separator = value.find("//");
        std::string source = separator == std::string::npos ? value : value.substr(0, separator);
        std::string subId = separator == std::string::npos ? std::string() : value.substr(separator);
        bool matches = source == oldKey;
        if (!matches && prefix && source.size() > oldKey.size())
        {
            matches = source.compare(0, oldKey.size(), oldKey) == 0 && source[oldKey.size()] == '/';
        }
        if (!matches) return false;

        if (newKey.empty())
        {
            mapped.clear();
            return true;
        }

        mapped = newKey + source.substr(oldKey.size()) + subId;
        return mapped != value;
    }

    //读取当前资源根目录。
    int32 ORBEDEN_NATIVE_CALL GetManagedResourceRoot(void*, uint8* buffer, int32 bufferSize)
    {
        return CopyUtf8(PathDefines::GetResourceRoot(), buffer, bufferSize);
    }

    //判断是否允许托管层修改资源。
    uint8 ORBEDEN_NATIVE_CALL CanModifyManagedAssets(void* context)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        return editor && editor->HasProject() && !editor->IsPlaying() ? 1 : 0;
    }

    //请求原生 Editor 重绘。
    void ORBEDEN_NATIVE_CALL RequestManagedRepaint(void* context)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (editor) editor->RequestRepaint();
    }

    //重映射原生对象资源引用
    int32 ORBEDEN_NATIVE_CALL RemapManagedLiveReferences(void* context,
        const uint8* oldKeyText,
        int32 oldKeyLength,
        const uint8* newKeyText,
        int32 newKeyLength,
        uint8 prefix)
    {
        std::string oldKey = ResourceManager::ToResourceKey(ReadUtf8(oldKeyText, oldKeyLength));
        std::string newKey = ResourceManager::ToResourceKey(ReadUtf8(newKeyText, newKeyLength));
        if (oldKey.empty()) return 0;

        int32 changed = 0;
        for (TypeId typeId = 0; typeId < Object::GetTypeCount(); ++typeId)
        {
            Type* type = Object::FindType(typeId);
            if (!type) continue;

            const List<Reflection::FieldInfo>& fields = type->GetFields();
            type->ForEachLiveObject([&](Object* object)
            {
                for (const Reflection::FieldInfo& field : fields)
                {
                    if (field.kind != Reflection::FieldKind::ObjectRef || !field.getter || !field.setter) continue;

                    std::string mapped;
                    if (!TryMapResourceKey(field.GetValueAsString(object), oldKey, newKey, prefix != 0, mapped)) continue;
                    if (field.SetValueFromString(object, mapped)) changed++;
                }
            });
        }

        //刷新资源引用缓存
        ResourceManager::Shutdown();
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (editor) editor->RequestRepaint();
        return changed;
    }

    //把一个 C# Panel 注册到原生 PanelManager
    uint8 ORBEDEN_NATIVE_CALL RegisterManagedPanel(void* context,
        int32 handle,
        const uint8* id,
        int32 idLength,
        const uint8* title,
        int32 titleLength,
        uint8 defaultVisible,
        float32 defaultWidth,
        float32 defaultHeight,
        int32 defaultDock,
        float32 defaultDockRatio,
        int32 order)
    {
        ManagedPanelRegistrationContext* registration = static_cast<ManagedPanelRegistrationContext*>(context);
        if (!registration || !registration->editor || !registration->panelManager || handle < 0) return 0;
        if (defaultDock < static_cast<int32>(PanelDockPlacement::Center)
            || defaultDock > static_cast<int32>(PanelDockPlacement::Floating))
        {
            Log::Error("Managed Panel registration failed: invalid default dock placement.");
            return 0;
        }

        EditorPanelInfo info;
        info.id = ReadUtf8(id, idLength);
        info.title = ReadUtf8(title, titleLength);
        info.defaultVisible = defaultVisible != 0;
        info.defaultSize = { defaultWidth, defaultHeight };
        info.defaultDock = static_cast<PanelDockPlacement>(defaultDock);
        info.defaultDockRatio = defaultDockRatio;
        info.order = order;
        return registration->panelManager->RegisterPanel(
            std::make_unique<ManagedPanelAdapter>(*registration->editor, std::move(info), handle)) ? 1 : 0;
    }
}

bool ManagedEditorBridge::Initialize(EditorClrHost& host,
    EditorSystem& editor,
    EditorGUI& editorGUI,
    PanelManager& panelManager,
    const EditorGizmoApi& gizmoApi,
    const std::string& executablePath)
{
    if (initialized) return true;
    if (!host.IsInitialized())
    {
        Log::Warning("ManagedEditorBridge initialize skipped: EditorClrHost is not initialized.");
        return false;
    }

    std::filesystem::path managedDirectory = GetExecutableDirectory(executablePath) / "Managed";
    std::string editorAssemblyPath = Utf8Path::ToUtf8((managedDirectory / "Orbeden.Editor.dll").lexically_normal());

    //绑定托管入口并注册面板
    clrHost = &host;
    ManagedInitializeEditorFn initializeEditor = nullptr;
    if (!clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorInitializeMethod,
        reinterpret_cast<void**>(&initializeEditor))
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorDrawPanelMethod, &DrawPanelFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorSetPanelVisibleMethod, &SetPanelVisibleFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorLoadGameAssemblyMethod, &LoadGameAssemblyFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorUnloadGameAssemblyMethod, &UnloadGameAssemblyFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorDrawSceneGizmosMethod, &DrawSceneGizmosFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorPublishGameAotMethod, &PublishGameAotFunction))
    {
        Log::Warning("ManagedEditorBridge initialize failed: managed entry binding failed.");
        Shutdown();
        return false;
    }

    //初始化托管运行时和面板元数据
    OrbedenEngineNativeApi engineApi = OrbedenEngineNativeApi::Create();
    ManagedPanelRegistrationContext panelContext { &editor, &panelManager };
    EditorManagedApi editorApi;
    editorApi.engineApi = &engineApi;
    editorApi.gui = editorGUI.GetNativeApi();
    editorApi.application.context = &editor;
    editorApi.application.requestRepaint = reinterpret_cast<void*>(&RequestManagedRepaint);
    editorApi.gizmo = gizmoApi;
    editorApi.panels.context = &panelContext;
    editorApi.panels.registerPanel = reinterpret_cast<void*>(&RegisterManagedPanel);
    editorApi.assets.context = &editor;
    editorApi.assets.getResourceRoot = reinterpret_cast<void*>(&GetManagedResourceRoot);
    editorApi.assets.canModifyAssets = reinterpret_cast<void*>(&CanModifyManagedAssets);
    editorApi.assets.remapLiveReferences = reinterpret_cast<void*>(&RemapManagedLiveReferences);
    if (initializeEditor(&editorApi) == 0)
    {
        Log::Warning("ManagedEditorBridge initialize failed: managed runtime rejected initialization.");
        Shutdown();
        return false;
    }

    initialized = true;
    return true;
}

void ManagedEditorBridge::Shutdown()
{
    DrawPanelFunction = nullptr;
    SetPanelVisibleFunction = nullptr;
    DrawSceneGizmosFunction = nullptr;
    LoadGameAssemblyFunction = nullptr;
    UnloadGameAssemblyFunction = nullptr;
    PublishGameAotFunction = nullptr;
    initialized = false;
    clrHost = nullptr;
}

void ManagedEditorBridge::DrawPanel(int32 handle, EnsId selectedEns, const std::string& stableId)
{
    if (!initialized || !DrawPanelFunction) return;

    ManagedDrawPanelFn drawPanel = reinterpret_cast<ManagedDrawPanelFn>(DrawPanelFunction);
    drawPanel(handle,
        selectedEns.id,
        selectedEns.version,
        reinterpret_cast<const uint8*>(stableId.data()),
        static_cast<int32>(stableId.size()));
}

void ManagedEditorBridge::SetPanelVisible(int32 handle, bool visible)
{
    if (!initialized || !SetPanelVisibleFunction) return;

    ManagedSetPanelVisibleFn setPanelVisible = reinterpret_cast<ManagedSetPanelVisibleFn>(SetPanelVisibleFunction);
    setPanelVisible(handle, visible ? 1 : 0);
}

void ManagedEditorBridge::LoadGameAssembly(const std::string& assemblyPath, const std::string& sidecarPath)
{
    if (!initialized || !LoadGameAssemblyFunction) return;

    ManagedLoadGameAssemblyFn loadGameAssembly = reinterpret_cast<ManagedLoadGameAssemblyFn>(LoadGameAssemblyFunction);
    loadGameAssembly(reinterpret_cast<const uint8*>(assemblyPath.data()),
        static_cast<int32>(assemblyPath.size()),
        reinterpret_cast<const uint8*>(sidecarPath.data()),
        static_cast<int32>(sidecarPath.size()));
}

void ManagedEditorBridge::UnloadGameAssembly()
{
    if (!initialized || !UnloadGameAssemblyFunction) return;

    ManagedUnloadGameAssemblyFn unloadGameAssembly = reinterpret_cast<ManagedUnloadGameAssemblyFn>(UnloadGameAssemblyFunction);
    unloadGameAssembly();
}

void ManagedEditorBridge::DrawSceneGizmos()
{
    if (!initialized || !DrawSceneGizmosFunction) return;

    ManagedDrawEditorFn drawSceneGizmos = reinterpret_cast<ManagedDrawEditorFn>(DrawSceneGizmosFunction);
    drawSceneGizmos();
}

bool ManagedEditorBridge::PublishGameAot(const std::string& repositoryRoot,
    const std::string& projectRoot,
    const std::string& scriptProject,
    const std::string& configuration,
    const std::string& targetPlatform,
    std::string& error)
{
    error.clear();
    if (!initialized || !PublishGameAotFunction)
    {
        error = "Editor managed NativeAOT publisher is not initialized.";
        return false;
    }

    std::array<uint8, 4096> errorBuffer{};
    ManagedPublishGameAotFn publishGameAot = reinterpret_cast<ManagedPublishGameAotFn>(PublishGameAotFunction);
    uint8 succeeded = publishGameAot(
        reinterpret_cast<const uint8*>(repositoryRoot.data()), static_cast<int32>(repositoryRoot.size()),
        reinterpret_cast<const uint8*>(projectRoot.data()), static_cast<int32>(projectRoot.size()),
        reinterpret_cast<const uint8*>(scriptProject.data()), static_cast<int32>(scriptProject.size()),
        reinterpret_cast<const uint8*>(configuration.data()), static_cast<int32>(configuration.size()),
        reinterpret_cast<const uint8*>(targetPlatform.data()), static_cast<int32>(targetPlatform.size()),
        errorBuffer.data(), static_cast<int32>(errorBuffer.size()));
    error = reinterpret_cast<const char*>(errorBuffer.data());
    return succeeded != 0;
}

bool ManagedEditorBridge::IsInitialized() const
{
    return initialized;
}
