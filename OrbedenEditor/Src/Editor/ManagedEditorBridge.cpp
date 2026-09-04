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
#include "Runtime/Ens.h"
#include "Runtime/Object/TransformComponent.h"
#include "Runtime/ResourceManager.h"
#include "Scripting/ScriptBehaviour.h"
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
    using ManagedDrawPanelFn = void(CORECLR_DELEGATE_CALLTYPE*)(
        int32, uint32, uint32, const EnsId*, int32, const uint8*, int32, const uint8*, int32);
    using ManagedSetPanelVisibleFn = void(CORECLR_DELEGATE_CALLTYPE*)(int32, uint8);
    using ManagedDrawEditorFn = void(CORECLR_DELEGATE_CALLTYPE*)();
    using ManagedLoadGameAssemblyFn = void(CORECLR_DELEGATE_CALLTYPE*)(const uint8*, int32);
    using ManagedUnloadGameAssemblyFn = void(CORECLR_DELEGATE_CALLTYPE*)();
    using ManagedCommandFn = uint8(CORECLR_DELEGATE_CALLTYPE*)();
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
    constexpr const char* EditorSaveProjectStateMethod = "SaveProjectState";
    constexpr const char* EditorUndoMethod = "Undo";
    constexpr const char* EditorRedoMethod = "Redo";
    constexpr const char* EditorWorldSavedMethod = "WorldSaved";

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

    //传给 Editor C# 的原生组件检查函数表。
    struct EditorComponentNativeApi
    {
    public:
        void* context = nullptr;
        void* getComponentCount = nullptr;
        void* getComponentObjectId = nullptr;
        void* getComponentTypeName = nullptr;
        void* getComponentDomain = nullptr;
        void* getFieldCount = nullptr;
        void* getFieldName = nullptr;
        void* getFieldKind = nullptr;
        void* getFieldValue = nullptr;
        void* setFieldValue = nullptr;
        void* setManagedField = nullptr;
        void* getAddableTypeCount = nullptr;
        void* getAddableTypeName = nullptr;
        void* addComponent = nullptr;
        void* removeComponent = nullptr;
    };

    //传给 Editor C# 的应用函数表。
    struct EditorApplicationNativeApi
    {
    public:
        void* context = nullptr;
        void* requestRepaint = nullptr;
        void* isPlaying = nullptr;
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
        EditorComponentNativeApi components;
    };

    #pragma pack(pop)

    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorPanelNativeApi, 2);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorAssetNativeApi, 4);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorApplicationNativeApi, 3);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorComponentNativeApi, 15);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorManagedApi, 57);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, engineApi, 0);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, application, 31);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, gizmo, 34);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, panels, 36);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, assets, 38);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, components, 42);

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

    //查询当前是否处于 Play-In-Editor。
    uint8 ORBEDEN_NATIVE_CALL IsManagedEditorPlaying(void* context)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        return editor && editor->IsPlaying() ? 1 : 0;
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

    //查找属于当前 Editor World 的组件对象。
    Component* FindEditorComponent(void* context, int32 objectId)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        Object* object = Object::FindObjectById(objectId);
        Component* component = object ? object->Cast<Component>() : nullptr;
        return editor && component && component->GetWorld() == &editor->GetWorld() ? component : nullptr;
    }

    //收集一个组件从基类到派生类的可见字段。
    List<const Reflection::FieldInfo*> GetEditorComponentFields(Component* component)
    {
        List<const Reflection::FieldInfo*> fields;
        if (component) Reflection::CollectFields(component->GetType(), fields);
        return fields;
    }
    //把精确 ScriptBehaviour 识别为 C# 脚本宿主。
    ScriptBehaviour* AsManagedScriptHost(Component* component)
    {
        ScriptBehaviour* script = component ? component->Cast<ScriptBehaviour>() : nullptr;
        return script && script->IsManagedHost() ? script : nullptr;
    }

    //收集允许在 Inspector 显示的 C# 动态字段。
    List<const ManagedScriptField*> GetManagedScriptFields(ScriptBehaviour* host)
    {
        List<const ManagedScriptField*> fields;
        if (!host) return fields;
        for (const ManagedScriptField& field : host->GetManagedFields())
        {
            if (field.inspectorVisible) fields.push_back(&field);
        }
        return fields;
    }


    int32 ORBEDEN_NATIVE_CALL GetManagedComponentCount(void* context, uint32 ensId, uint32 ensVersion)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        Ens* ens = editor ? editor->GetWorld().GetEns({ ensId, ensVersion }) : nullptr;
        return ens ? static_cast<int32>(ens->GetComponents().size()) : 0;
    }

    int32 ORBEDEN_NATIVE_CALL GetManagedComponentObjectId(void* context, uint32 ensId, uint32 ensVersion, int32 index)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        Ens* ens = editor ? editor->GetWorld().GetEns({ ensId, ensVersion }) : nullptr;
        if (!ens || index < 0 || index >= static_cast<int32>(ens->GetComponents().size())) return 0;

        Component* component = ens->GetComponents()[index];
        return component ? component->GetObjectId() : 0;
    }

    int32 ORBEDEN_NATIVE_CALL GetManagedComponentTypeName(void* context, int32 objectId, uint8* buffer, int32 bufferSize)
    {
        Component* component = FindEditorComponent(context, objectId);
        ScriptBehaviour* host = AsManagedScriptHost(component);
        if (host)
        {
            const std::string& typeName = host->GetManagedTypeName();
            return CopyUtf8(typeName.empty() ? std::string("Missing Script") : typeName, buffer, bufferSize);
        }
        return component ? CopyUtf8(component->GetType()->GetName(), buffer, bufferSize) : 0;
    }

    int32 ORBEDEN_NATIVE_CALL GetManagedComponentDomain(void* context, int32 objectId)
    {
        return AsManagedScriptHost(FindEditorComponent(context, objectId)) ? 1 : 0;
    }

    int32 ORBEDEN_NATIVE_CALL GetManagedComponentFieldCount(void* context, int32 objectId)
    {
        Component* component = FindEditorComponent(context, objectId);
        ScriptBehaviour* host = AsManagedScriptHost(component);
        if (host) return 1 + static_cast<int32>(GetManagedScriptFields(host).size());
        return static_cast<int32>(GetEditorComponentFields(component).size());
    }

    int32 ORBEDEN_NATIVE_CALL GetManagedComponentFieldName(void* context, int32 objectId, int32 fieldIndex, uint8* buffer, int32 bufferSize)
    {
        Component* component = FindEditorComponent(context, objectId);
        ScriptBehaviour* host = AsManagedScriptHost(component);
        if (host)
        {
            if (fieldIndex == 0) return CopyUtf8("enabled", buffer, bufferSize);
            List<const ManagedScriptField*> fields = GetManagedScriptFields(host);
            --fieldIndex;
            if (fieldIndex < 0 || fieldIndex >= static_cast<int32>(fields.size())) return 0;
            return CopyUtf8(fields[fieldIndex]->name, buffer, bufferSize);
        }

        List<const Reflection::FieldInfo*> fields = GetEditorComponentFields(component);
        if (fieldIndex < 0 || fieldIndex >= static_cast<int32>(fields.size()) || !fields[fieldIndex]) return 0;
        return CopyUtf8(fields[fieldIndex]->name ? fields[fieldIndex]->name : "", buffer, bufferSize);
    }

    int32 ORBEDEN_NATIVE_CALL GetManagedComponentFieldKind(void* context, int32 objectId, int32 fieldIndex)
    {
        Component* component = FindEditorComponent(context, objectId);
        ScriptBehaviour* host = AsManagedScriptHost(component);
        if (host)
        {
            if (fieldIndex == 0) return static_cast<int32>(Reflection::FieldKind::Bool);
            List<const ManagedScriptField*> fields = GetManagedScriptFields(host);
            --fieldIndex;
            return fieldIndex >= 0 && fieldIndex < static_cast<int32>(fields.size())
                ? static_cast<int32>(fields[fieldIndex]->kind)
                : 0;
        }

        List<const Reflection::FieldInfo*> fields = GetEditorComponentFields(component);
        if (fieldIndex < 0 || fieldIndex >= static_cast<int32>(fields.size()) || !fields[fieldIndex]) return 0;
        return static_cast<int32>(fields[fieldIndex]->kind);
    }

    int32 ORBEDEN_NATIVE_CALL GetManagedComponentFieldValue(void* context, int32 objectId, int32 fieldIndex, uint8* buffer, int32 bufferSize)
    {
        Component* component = FindEditorComponent(context, objectId);
        ScriptBehaviour* host = AsManagedScriptHost(component);
        if (host)
        {
            if (fieldIndex == 0) return CopyUtf8(host->GetEnabled() ? "true" : "false", buffer, bufferSize);
            List<const ManagedScriptField*> fields = GetManagedScriptFields(host);
            --fieldIndex;
            if (fieldIndex < 0 || fieldIndex >= static_cast<int32>(fields.size())) return 0;
            return CopyUtf8(fields[fieldIndex]->value, buffer, bufferSize);
        }

        List<const Reflection::FieldInfo*> fields = GetEditorComponentFields(component);
        if (!component || fieldIndex < 0 || fieldIndex >= static_cast<int32>(fields.size()) || !fields[fieldIndex]) return 0;
        return CopyUtf8(fields[fieldIndex]->GetValueAsString(component), buffer, bufferSize);
    }

    uint8 ORBEDEN_NATIVE_CALL SetManagedComponentFieldValue(void* context,
        int32 objectId,
        int32 fieldIndex,
        const uint8* value,
        int32 valueLength)
    {
        Component* component = FindEditorComponent(context, objectId);
        ScriptBehaviour* host = AsManagedScriptHost(component);
        std::string valueText = ReadUtf8(value, valueLength);
        if (host)
        {
            if (fieldIndex == 0)
            {
                if (valueText != "true" && valueText != "false" && valueText != "1" && valueText != "0") return 0;
                host->SetEnabled(valueText == "true" || valueText == "1");
                return 1;
            }

            List<const ManagedScriptField*> fields = GetManagedScriptFields(host);
            --fieldIndex;
            return fieldIndex >= 0
                && fieldIndex < static_cast<int32>(fields.size())
                && host->SetManagedFieldValue(fields[fieldIndex]->name, valueText) ? 1 : 0;
        }

        List<const Reflection::FieldInfo*> fields = GetEditorComponentFields(component);
        if (!component || fieldIndex < 0 || fieldIndex >= static_cast<int32>(fields.size()) || !fields[fieldIndex]) return 0;
        return fields[fieldIndex]->SetValueFromString(component, valueText) ? 1 : 0;
    }

    uint8 ORBEDEN_NATIVE_CALL SetManagedScriptField(void* context,
        int32 objectId,
        const uint8* name,
        int32 nameLength,
        const uint8* typeName,
        int32 typeNameLength,
        const uint8* value,
        int32 valueLength,
        uint8 inspectorVisible)
    {
        ScriptBehaviour* host = AsManagedScriptHost(FindEditorComponent(context, objectId));
        if (!host) return 0;
        std::string fieldType = ReadUtf8(typeName, typeNameLength);
        return host->SetManagedField(
            ReadUtf8(name, nameLength),
            fieldType,
            ScriptBehaviour::GetManagedFieldKind(fieldType),
            ReadUtf8(value, valueLength),
            inspectorVisible != 0) ? 1 : 0;
    }

    //按类型注册顺序枚举可创建的原生组件类型。
    List<Type*> GetAddableNativeComponentTypes()
    {
        List<Type*> types;
        for (TypeId typeId = 0; typeId < Object::GetTypeCount(); ++typeId)
        {
            Type* type = Object::FindType(typeId);
            if (!type || type == Component::StaticType() || type == TransformComponent::StaticType() || type == ScriptBehaviour::StaticType()) continue;
            if (type->Is(Component::StaticType()) && type->CanCreateObject()) types.push_back(type);
        }
        return types;
    }

    int32 ORBEDEN_NATIVE_CALL GetManagedAddableComponentTypeCount(void*)
    {
        return static_cast<int32>(GetAddableNativeComponentTypes().size());
    }

    int32 ORBEDEN_NATIVE_CALL GetManagedAddableComponentTypeName(void*, int32 index, uint8* buffer, int32 bufferSize)
    {
        List<Type*> types = GetAddableNativeComponentTypes();
        if (index < 0 || index >= static_cast<int32>(types.size())) return 0;
        return CopyUtf8(types[index]->GetName(), buffer, bufferSize);
    }

    int32 ORBEDEN_NATIVE_CALL AddManagedNativeComponent(void* context,
        uint32 ensId,
        uint32 ensVersion,
        const uint8* typeName,
        int32 typeNameLength)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        Ens* ens = editor ? editor->GetWorld().GetEns({ ensId, ensVersion }) : nullptr;
        if (!ens) return 0;

        std::string requestedType = ReadUtf8(typeName, typeNameLength);
        Type* type = Object::FindType(requestedType);
        Component* component = nullptr;
        if (type && type->Is(Component::StaticType()) && type->CanCreateObject())
        {
            component = type->Is(ScriptBehaviour::StaticType())
                ? ens->AddComponentInstance(type)
                : ens->AddComponent(type);
        }
        else
        {
            Component* createdHost = ens->AddComponentInstance(ScriptBehaviour::StaticType());
            ScriptBehaviour* host = createdHost ? createdHost->Cast<ScriptBehaviour>() : nullptr;
            if (host && host->SetManagedTypeName(requestedType)) component = host;
            else if (host) ens->RemoveComponent(host);
        }
        return component ? component->GetObjectId() : 0;
    }

    uint8 ORBEDEN_NATIVE_CALL RemoveManagedNativeComponent(void* context, int32 objectId)
    {
        Component* component = FindEditorComponent(context, objectId);
        if (!component || component->GetType() == TransformComponent::StaticType()) return 0;
        Ens* ens = component->GetEns();
        return ens && ens->RemoveComponent(component) ? 1 : 0;
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
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorPublishGameAotMethod, &PublishGameAotFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorSaveProjectStateMethod, &SaveProjectStateFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorUndoMethod, &UndoFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorRedoMethod, &RedoFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorWorldSavedMethod, &WorldSavedFunction))
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
    editorApi.application.isPlaying = reinterpret_cast<void*>(&IsManagedEditorPlaying);
    editorApi.gizmo = gizmoApi;
    editorApi.panels.context = &panelContext;
    editorApi.panels.registerPanel = reinterpret_cast<void*>(&RegisterManagedPanel);
    editorApi.assets.context = &editor;
    editorApi.assets.getResourceRoot = reinterpret_cast<void*>(&GetManagedResourceRoot);
    editorApi.assets.canModifyAssets = reinterpret_cast<void*>(&CanModifyManagedAssets);
    editorApi.assets.remapLiveReferences = reinterpret_cast<void*>(&RemapManagedLiveReferences);
    editorApi.components.context = &editor;
    editorApi.components.getComponentCount = reinterpret_cast<void*>(&GetManagedComponentCount);
    editorApi.components.getComponentObjectId = reinterpret_cast<void*>(&GetManagedComponentObjectId);
    editorApi.components.getComponentTypeName = reinterpret_cast<void*>(&GetManagedComponentTypeName);
    editorApi.components.getComponentDomain = reinterpret_cast<void*>(&GetManagedComponentDomain);
    editorApi.components.getFieldCount = reinterpret_cast<void*>(&GetManagedComponentFieldCount);
    editorApi.components.getFieldName = reinterpret_cast<void*>(&GetManagedComponentFieldName);
    editorApi.components.getFieldKind = reinterpret_cast<void*>(&GetManagedComponentFieldKind);
    editorApi.components.getFieldValue = reinterpret_cast<void*>(&GetManagedComponentFieldValue);
    editorApi.components.setFieldValue = reinterpret_cast<void*>(&SetManagedComponentFieldValue);
    editorApi.components.setManagedField = reinterpret_cast<void*>(&SetManagedScriptField);
    editorApi.components.getAddableTypeCount = reinterpret_cast<void*>(&GetManagedAddableComponentTypeCount);
    editorApi.components.getAddableTypeName = reinterpret_cast<void*>(&GetManagedAddableComponentTypeName);
    editorApi.components.addComponent = reinterpret_cast<void*>(&AddManagedNativeComponent);
    editorApi.components.removeComponent = reinterpret_cast<void*>(&RemoveManagedNativeComponent);
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
    SaveProjectStateFunction = nullptr;
    UndoFunction = nullptr;
    RedoFunction = nullptr;
    WorldSavedFunction = nullptr;
    initialized = false;
    clrHost = nullptr;
}

void ManagedEditorBridge::DrawPanel(int32 handle,
    EnsId selectedEns,
    const EnsId* selectedEnsList,
    int32 selectedEnsCount,
    const std::string& selectedStableIds,
    const std::string& stableId)
{
    if (!initialized || !DrawPanelFunction) return;

    ManagedDrawPanelFn drawPanel = reinterpret_cast<ManagedDrawPanelFn>(DrawPanelFunction);
    drawPanel(handle,
        selectedEns.id,
        selectedEns.version,
        selectedEnsList,
        selectedEnsCount,
        reinterpret_cast<const uint8*>(selectedStableIds.data()),
        static_cast<int32>(selectedStableIds.size()),
        reinterpret_cast<const uint8*>(stableId.data()),
        static_cast<int32>(stableId.size()));
}

void ManagedEditorBridge::SetPanelVisible(int32 handle, bool visible)
{
    if (!initialized || !SetPanelVisibleFunction) return;

    ManagedSetPanelVisibleFn setPanelVisible = reinterpret_cast<ManagedSetPanelVisibleFn>(SetPanelVisibleFunction);
    setPanelVisible(handle, visible ? 1 : 0);
}

void ManagedEditorBridge::LoadGameAssembly(const std::string& assemblyPath)
{
    if (!initialized || !LoadGameAssemblyFunction) return;

    ManagedLoadGameAssemblyFn loadGameAssembly = reinterpret_cast<ManagedLoadGameAssemblyFn>(LoadGameAssemblyFunction);
    loadGameAssembly(reinterpret_cast<const uint8*>(assemblyPath.data()),
        static_cast<int32>(assemblyPath.size()));
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

bool ManagedEditorBridge::SaveProjectState()
{
    if (!initialized || !SaveProjectStateFunction) return true;
    ManagedCommandFn saveProjectState = reinterpret_cast<ManagedCommandFn>(SaveProjectStateFunction);
    return saveProjectState() != 0;
}

bool ManagedEditorBridge::Undo()
{
    if (!initialized || !UndoFunction) return false;
    ManagedCommandFn undo = reinterpret_cast<ManagedCommandFn>(UndoFunction);
    return undo() != 0;
}

bool ManagedEditorBridge::Redo()
{
    if (!initialized || !RedoFunction) return false;
    ManagedCommandFn redo = reinterpret_cast<ManagedCommandFn>(RedoFunction);
    return redo() != 0;
}

void ManagedEditorBridge::NotifyWorldSaved()
{
    if (!initialized || !WorldSavedFunction) return;
    ManagedDrawEditorFn worldSaved = reinterpret_cast<ManagedDrawEditorFn>(WorldSavedFunction);
    worldSaved();
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
