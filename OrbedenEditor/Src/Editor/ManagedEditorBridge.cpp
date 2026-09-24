#include "Editor/ManagedEditorBridge.h"

#include "Editor/EditorScene.h"
#include "Editor/EditorSystem.h"
#include "Editor/NewProjectTemplate.h"
#include "Editor/EditorGUI.h"
#include "Editor/PanelManager.h"
#include "Editor/Panels/ManagedPanelAdapter.h"
#include "FileSystem/PathDefines.h"
#include "FileSystem/Utf8Path.h"
#include "Platform/ExecutablePath.h"
#include "Log/Log.h"
#include "Profiler/Profiler.h"
#include "Runtime/Reflection.h"
#include "Runtime/CookedAssetSerializer.h"
#include "Runtime/CookedAssetSerializer.h"
#include "Scripting/ScriptInterop.h"
#include <unordered_map>
#include <string_view>
#include "Runtime/Object/Ens.h"
#include "Runtime/WorldSerializer.h"
#include "Runtime/AssetPipeline.h"
#include "Runtime/Object/Material.h"
#include "Runtime/Object/Transform.h"
#include "ResourceManager/ResourceManager.h"
#include "Runtime/Object/Script.h"
#include "Runtime/Object/Texture2D.h"
#include "Runtime/Native/NativeCall.h"
#include "Runtime/Native/NativeApiAbi.h"
#include "Runtime/Native/OrbedenEngineNativeApi.h"
#include "Runtime/Native/OrbedenNativeApi.h"

#include <coreclr_delegates.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <utility>

namespace
{
    using ManagedInitializeEditorFn = uint8(CORECLR_DELEGATE_CALLTYPE*)(void*);
    using ManagedGetInitializationErrorFn = void*(CORECLR_DELEGATE_CALLTYPE*)(int32*);
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
    constexpr const char* EditorGetInitializationErrorMethod = "GetInitializationError";
    constexpr const char* EditorDrawPanelMethod = "DrawPanel";
    constexpr const char* EditorSetPanelVisibleMethod = "SetPanelVisible";
    constexpr const char* EditorLoadGameAssemblyMethod = "LoadGameAssembly";
    constexpr const char* EditorUnloadGameAssemblyMethod = "UnloadGameAssembly";
    constexpr const char* EditorDrawSceneGizmosMethod = "DrawSceneGizmos";
    constexpr const char* EditorDrawStatusBarMethod = "DrawStatusBar";
    constexpr const char* EditorPublishGameAotMethod = "PublishGameAot";
    constexpr const char* EditorSaveProjectStateMethod = "SaveProjectState";
    constexpr const char* EditorUndoMethod = "Undo";
    constexpr const char* EditorRedoMethod = "Redo";
    constexpr const char* EditorRequestRenameSelectedMethod = "RequestRenameSelected";
    constexpr const char* EditorRequestDeleteSelectedMethod = "RequestDeleteSelected";
    constexpr const char* EditorRequestReimportSelectedMethod = "RequestReimportSelected";
    constexpr const char* EditorRequestReimportAllMethod = "RequestReimportAll";
    constexpr const char* EditorRequestCopySelectedMethod = "RequestCopySelected";
    constexpr const char* EditorRequestPasteSelectedMethod = "RequestPasteSelected";
    constexpr const char* EditorRequestToggleActiveSelectedMethod = "RequestToggleActiveSelected";

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
        void* canModifyAssets = nullptr;
        void* remapLiveReferences = nullptr;
        void* openWorld = nullptr;
        void* getWorldKey = nullptr;
        void* saveWorld = nullptr;
        void* setStartupWorld = nullptr;
        void* createWorld = nullptr;
        void* remapWorldKeys = nullptr;
        void* getProjectError = nullptr;
        void* savePrefab = nullptr;
        void* instantiatePrefab = nullptr;
        void* captureEns = nullptr;
        void* destroyEnsTree = nullptr;
        void* createEns = nullptr;
        void* reimportAsset = nullptr;
        void* reimportAllAssets = nullptr;
        void* loadCachedAsset = nullptr;
        void* saveMaterial = nullptr;
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
        void* readComponentSnapshot = nullptr;
        void* setComponentProperty = nullptr;
        void* getRegistryGeneration = nullptr;
        void* setManagedField = nullptr;
        void* getAddableTypeCount = nullptr;
        void* getAddableTypeName = nullptr;
        void* addComponent = nullptr;
        void* removeComponent = nullptr;
        void* captureComponent = nullptr;
        void* restoreComponent = nullptr;
        void* findComponent = nullptr;
        void* getHostBinding = nullptr;
        void* getWorldEns = nullptr;
        void* selectEns = nullptr;
        void* matchComponentType = nullptr;
        void* getReferenceObjects = nullptr;
        void* getReferenceLabel = nullptr;
        void* moveEns = nullptr;
        void* focusEns = nullptr;
    };

    //传给 Editor C# 的应用函数表。
    struct EditorApplicationNativeApi
    {
    public:
        void* context = nullptr;
        void* requestRepaint = nullptr;
        void* isPlaying = nullptr;
        void* getProjectText = nullptr;
        void* requestBuild = nullptr;
        void* getSelectedPlayerTarget = nullptr;
        void* setSelectedPlayerTarget = nullptr;
        void* mirrorTemplate = nullptr;
        void* isWorldDirty = nullptr;
        void* setWorldDirty = nullptr;
    };

    //传给 Editor C# 的日志函数表。
    struct EditorLogNativeApi
    {
    public:
        void* getRange = nullptr;
        void* copyEntry = nullptr;
        void* getCounts = nullptr;
        void* append = nullptr;
        void* clear = nullptr;
    };

    //传给 Editor C# 的性能剖析函数表。
    struct EditorProfilerNativeApi
    {
    public:
        void* setCapturing = nullptr;
        void* isCapturing = nullptr;
        void* clearFrames = nullptr;
        void* getFrameRange = nullptr;
        void* copyFrameSummaries = nullptr;
        void* copyFrameEvents = nullptr;
        void* getNameCount = nullptr;
        void* copyName = nullptr;
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
        EditorLogNativeApi log;
        EditorProfilerNativeApi profiler;
    };

    #pragma pack(pop)

    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorPanelNativeApi, 2);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorAssetNativeApi, 19);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorApplicationNativeApi, 10);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorComponentNativeApi, 24);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorLogNativeApi, 5);
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorProfilerNativeApi, 8);
    //gui 表扩容后，排在它后面的每张表偏移都跟着后移
    ORBEDEN_ASSERT_NATIVE_API_TABLE(EditorManagedApi, 145);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, engineApi, 0);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, application, 74);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, gizmo, 84);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, panels, 87);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, assets, 89);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, components, 108);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, log, 132);
    ORBEDEN_ASSERT_NATIVE_API_SLOT(EditorManagedApi, profiler, 137);

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

    //重写 '|' 连接的引用列表文本，空槽与分隔符原样保留
    bool TryMapResourceKeyList(const std::string& value, const std::string& oldKey, const std::string& newKey,
        bool prefix, std::string& mapped)
    {
        std::string result;
        usize start = 0;
        bool changed = false;
        while (true)
        {
            usize separator = value.find(Reflection::ReferenceListSeparator, start);
            usize length = separator == std::string::npos ? std::string::npos : separator - start;
            std::string entry = value.substr(start, length);
            std::string entryMapped;
            if (TryMapResourceKey(entry, oldKey, newKey, prefix, entryMapped))
            {
                entry = entryMapped;
                changed = true;
            }
            result += entry;
            if (separator == std::string::npos) break;
            result += Reflection::ReferenceListSeparator;
            start = separator + 1;
        }
        mapped = result;
        return changed;
    }

    //判断是否允许托管层修改资源。
    uint8 ORBEDEN_NATIVE_CALL CanModifyManagedAssets(void* context)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        return editor && editor->HasProject() && !editor->IsPlaying() ? 1 : 0;
    }

    //打开项目内的另一个场景。
    uint8 ORBEDEN_NATIVE_CALL OpenManagedWorld(void* context, const uint8* keyText, int32 keyLength)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        return editor && editor->OpenWorld(ReadUtf8(keyText, keyLength)) ? 1 : 0;
    }

    //读取编辑 World 或启动 World 的资源 Key
    int32 ORBEDEN_NATIVE_CALL GetManagedWorldKey(void* context, uint8 startup, uint8* buffer, int32 capacity)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor) return 0;
        const std::string& key = startup ? editor->GetProject().GetStartupWorldKey() : editor->GetProject().GetCurrentWorldKey();
        if (buffer && capacity >= static_cast<int32>(key.size())) std::memcpy(buffer, key.data(), key.size());
        return static_cast<int32>(key.size());
    }

    //保存当前编辑 World
    uint8 ORBEDEN_NATIVE_CALL SaveManagedWorld(void* context)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        return editor && !editor->IsPlaying() && editor->SaveCurrentWorld() ? 1 : 0;
    }

    //设置启动 World
    uint8 ORBEDEN_NATIVE_CALL SetManagedStartupWorld(void* context, const uint8* key, int32 length)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        return editor && !editor->IsPlaying() && editor->GetProject().SetStartupWorld(ReadUtf8(key, length)) ? 1 : 0;
    }

    //创建空 World 文件
    uint8 ORBEDEN_NATIVE_CALL CreateManagedWorld(void* context, const uint8* key, int32 length)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        return editor && !editor->IsPlaying() && editor->GetProject().CreateWorld(ReadUtf8(key, length)) ? 1 : 0;
    }

    //同步资产移动后的 World 配置
    uint8 ORBEDEN_NATIVE_CALL RemapManagedWorldKeys(void* context, const uint8* oldKey, int32 oldLength,
        const uint8* newKey, int32 newLength, uint8 prefix)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        return editor && !editor->IsPlaying()
            && editor->GetProject().RemapWorldKeys(ReadUtf8(oldKey, oldLength), ReadUtf8(newKey, newLength), prefix != 0) ? 1 : 0;
    }

    //把当前 World 的 Ens 子树保存为独立预制体
    uint8 ORBEDEN_NATIVE_CALL SaveManagedPrefab(void* context, const uint8* source, int32 sourceLength,
        const uint8* key, int32 keyLength)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || editor->IsPlaying() || !editor->HasProject()) return 0;
        Ens* ens = editor->GetWorld().FindEns(StringId(ReadUtf8(source, sourceLength)));
        if (!ens || editor->GetEditorScene().IsTemporaryEns(ens->GetId())) return 0;
        std::filesystem::path relative = Utf8Path::FromUtf8(ReadUtf8(key, keyLength)).lexically_normal();
        if (relative.empty() || relative.has_root_path() || *relative.begin() == ".." || relative.extension() != ".prefab") return 0;
        std::filesystem::path path = Utf8Path::FromUtf8(editor->GetProjectContentRootPath()) / relative;
        if (std::filesystem::exists(path)) return 0;
        std::string error;
        bool saved = WorldSerializer::SavePrefab(*ens, Utf8Path::ToUtf8(path), error);
        if (!saved) Log::Error(error.c_str());
        return saved ? 1 : 0;
    }

    //把材质资产写回它的源文件
    uint8 ORBEDEN_NATIVE_CALL SaveManagedMaterial(void* context, int32 objectId, const uint8* key, int32 length)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || editor->IsPlaying() || !editor->HasProject()) return 0;
        Object* object = Object::FindObjectById(objectId);
        Material* material = object ? object->Cast<Material>() : nullptr;
        if (!material) return 0;
        std::filesystem::path relative = Utf8Path::FromUtf8(ReadUtf8(key, length)).lexically_normal();
        if (relative.empty() || relative.has_root_path() || *relative.begin() == ".."
            || relative.extension() != ".orbmat") return 0;
        std::filesystem::path path = Utf8Path::FromUtf8(editor->GetProjectContentRootPath()) / relative;
        std::string error;
        bool saved = AssetPipeline::SaveMaterialAsset(*material, Utf8Path::ToUtf8(path), error);
        if (!saved) Log::Error(error.c_str());
        return saved ? 1 : 0;
    }

    //销毁完整子树并清理失效选择
    uint8 ORBEDEN_NATIVE_CALL DestroyManagedEnsTree(void* context, EnsId root)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || editor->IsPlaying() || !editor->GetWorld().IsAlive(root)
            || editor->GetEditorScene().IsTemporaryEns(root)) return 0;
        World& world = editor->GetWorld();
        List<EnsId> tree { root };
        for (usize index = 0; index < tree.size(); ++index)
        {
            Transform* transform = world.GetTransform(tree[index]);
            for (EnsId child = transform->firstChild; !child.IsNull(); child = world.GetTransform(child)->next)
                tree.push_back(child);
        }
        for (auto it = tree.rbegin(); it != tree.rend(); ++it) world.DestroyEns(*it);
        editor->GetEditorScene().PruneSelection(world);
        editor->RequestRepaint();
        return 1;
    }

    //捕获子树快照供撤销恢复
    int32 ORBEDEN_NATIVE_CALL CaptureManagedEns(void* context, EnsId root, uint8* buffer, int32 capacity)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        Ens* ens = editor ? editor->GetWorld().GetEns(root) : nullptr;
        return CopyUtf8(ens ? WorldSerializer::CaptureEns(*ens) : std::string(), buffer, capacity);
    }

    //实例化资产、恢复快照或复制快照并设置层级顺序
    //kind：0 预制体文件、1 按快照恢复原身份（撤销）、2 按快照复制成新身份（复制粘贴）
    int32 ORBEDEN_NATIVE_CALL InstantiateManagedPrefab(void* context, const uint8* text, int32 length,
        uint8 kind, EnsId parent, EnsId before)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || editor->IsPlaying() || !editor->HasProject()) return 0;
        World& world = editor->GetWorld();
        if ((!parent.IsNull() && (!world.IsAlive(parent) || editor->GetEditorScene().IsTemporaryEns(parent)))
            || (!before.IsNull() && (!world.IsAlive(before) || world.GetTransform(before)->parent != parent))) return 0;
        std::string value = ReadUtf8(text, length), error;
        Ens* root = nullptr;
        if (kind == 1) root = WorldSerializer::RestoreEns(world, value, parent, error);
        else if (kind == 2) root = WorldSerializer::CopyEns(world, value, parent, error);
        else
        {
            std::filesystem::path relative = Utf8Path::FromUtf8(value).lexically_normal();
            if (relative.empty() || relative.has_root_path() || *relative.begin() == ".." || relative.extension() != ".prefab") return 0;
            root = WorldSerializer::InstantiatePrefab(world,
                Utf8Path::ToUtf8(Utf8Path::FromUtf8(editor->GetProjectContentRootPath()) / relative), parent, error);
        }
        if (!root) { Log::Error(error.c_str()); return 0; }
        if (!world.MoveEns(root->GetId(), parent, before))
        { DestroyManagedEnsTree(context, root->GetId()); return 0; }
        editor->GetEditorScene().SelectEns(root->GetId());
        editor->RequestRepaint();
        return root->GetObjectId();
    }

    //在当前 World 根下创建空 Ens 并返回对象号
    int32 ORBEDEN_NATIVE_CALL CreateManagedEns(void* context, const uint8* name, int32 nameLength)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || editor->IsPlaying() || !editor->HasProject()) return 0;

        Ens* ens = editor->GetWorld().CreateEns(ReadUtf8(name, nameLength));
        if (!ens) return 0;
        editor->GetEditorScene().SelectEns(ens->GetId());
        editor->RequestRepaint();
        return ens->GetObjectId();
    }

    //读取项目操作失败原因
    int32 ORBEDEN_NATIVE_CALL GetManagedProjectError(void* context, uint8* buffer, int32 capacity)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor) return 0;
        const std::string& error = editor->GetProject().GetLastError();
        if (buffer && capacity >= static_cast<int32>(error.size())) std::memcpy(buffer, error.data(), error.size());
        return static_cast<int32>(error.size());
    }

    //读取项目路径、构建状态与可选平台
    int32 ORBEDEN_NATIVE_CALL GetManagedProjectText(void* context, int32 field, uint8* buffer, int32 capacity)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor) return 0;
        std::string text;
        switch (field)
        {
        case 0: text = editor->GetProjectName(); break;
        case 1: text = editor->GetProjectRoot(); break;
        case 2: text = editor->GetProjectContentRootPath(); break;
        case 3: text = editor->GetWorldPath(); break;
        case 4: text = editor->GetProjectManagedRootPath(); break;
        case 5: text = editor->GetProjectNativeBuildPath(); break;
        case 6: text = editor->GetRepositoryRoot(); break;
        case 7: text = editor->GetSourceTemplateRoot(); break;
        case 8: text = editor->GetProjectStatusText(); break;
        case 9:
            for (int32 index = 0; index < editor->GetPlayerTargetPlatformCount(); ++index)
            {
                text += editor->GetPlayerTargetPlatformName(index);
                text.push_back(0);
                text += editor->IsPlayerTargetPlatformAvailable(index) ? "1" : "0";
                text.push_back(0);
            }
            break;
        }
        return CopyUtf8(text, buffer, capacity);
    }

    //请求现有原生构建流程
    void ORBEDEN_NATIVE_CALL RequestManagedBuild(void* context, int32 kind)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || !editor->HasProject()) return;
        if (kind == 0) editor->RequestBuildScripts();
        else if (kind == 1) editor->RequestBuildNative();
        else if (kind == 2) editor->RequestBuildPlayer();
    }

    //读取当前 Player 构建目标
    int32 ORBEDEN_NATIVE_CALL GetManagedPlayerTarget(void* context)
    {
        return static_cast<EditorSystem*>(context)->GetSelectedPlayerTargetPlatformIndex();
    }

    //设置可用的 Player 构建目标
    void ORBEDEN_NATIVE_CALL SetManagedPlayerTarget(void* context, int32 index)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (index >= 0 && index < editor->GetPlayerTargetPlatformCount() && editor->IsPlayerTargetPlatformAvailable(index))
            editor->SetSelectedPlayerTargetPlatformIndex(index);
    }

    //镜像的模板内容目录位，与 EditorApplication.cs 的 EditorTemplateFolder 对应
    constexpr uint8 TemplateFolderExamples = 1;
    constexpr uint8 TemplateFolderBuiltin = 2;

    //同步选中的模板内容目录并返回本次操作报告
    int32 ORBEDEN_NATIVE_CALL MirrorManagedTemplate(void* context, uint8 reset, uint8 folders, uint8* buffer, int32 capacity)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        std::string report;
        try
        {
            if (!editor || !editor->HasProject() || editor->IsPlaying())
                report = "Load a project and stop Play-In-Editor first.";
            else
            {
                std::string templates = editor->GetSourceTemplateRoot();
                std::string content = editor->GetProjectContentRootPath();
                if (templates.empty()) report = "The source template was not found.";
                else if (!editor->SaveCurrentWorld()) report = editor->GetProjectStatusText();
                else
                {
                    //写回方向示例与 Builtin 一起走：示例引用 Builtin，只写回一半会让材质指向不存在的默认资源。
                    //重置方向按位选择，可以只回退改过的那一侧，不牵动另一侧的项目改动。
                    NewProjectTemplate::MirrorReport changes;
                    std::string error;
                    std::string skipped;
                    std::string mirroredFolders;
                    bool mirrored = true;
                    for (const auto& folder : { std::make_pair(TemplateFolderExamples, NewProjectTemplate::ExamplesFolderName),
                        std::make_pair(TemplateFolderBuiltin, NewProjectTemplate::BuiltinFolderName) })
                    {
                        if ((folders & folder.first) == 0) continue;
                        std::string source = Utf8Path::ToUtf8(Utf8Path::FromUtf8(reset ? templates : content) / folder.second);
                        std::string target = Utf8Path::ToUtf8(Utf8Path::FromUtf8(reset ? content : templates) / folder.second);
                        //某一侧缺这个目录就跳过（例如 Builtin 之前的旧项目），不让单个目录废掉整次迁移
                        if (!std::filesystem::is_directory(Utf8Path::FromUtf8(source)))
                        {
                            skipped += " ";
                            skipped += folder.second;
                            continue;
                        }
                        NewProjectTemplate::MirrorReport folderChanges;
                        if (!NewProjectTemplate::MirrorTree(source, target, folderChanges, error))
                        {
                            mirrored = false;
                            break;
                        }
                        changes.added += folderChanges.added;
                        changes.updated += folderChanges.updated;
                        changes.removed += folderChanges.removed;
                        if (!mirroredFolders.empty()) mirroredFolders += "+";
                        mirroredFolders += folder.second;
                    }

                    if (!mirrored) report = error;
                    else if (mirroredFolders.empty())
                    {
                        report = "No template folders to mirror.";
                        if (!skipped.empty()) report += " Skipped missing folders:" + skipped;
                    }
                    else
                    {
                        report = std::string(reset ? "Restored template content (" : "Wrote back template content (")
                            + mirroredFolders + "): " + std::to_string(changes.added) + " added, "
                            + std::to_string(changes.updated) + " updated, " + std::to_string(changes.removed) + " removed.";
                        if (!skipped.empty()) report += "\nSkipped missing folders:" + skipped;
                        if (reset && !editor->ReloadProjectContent())
                            report += "\nContent reload failed: " + editor->GetProjectStatusText();
                        if (!reset) report += "\nReview with: git diff OrbedenEditor/Templates";
                    }
                }
            }
        }
        catch (const std::exception& exception) { report = exception.what(); }
        int32 count = std::min(capacity, static_cast<int32>(report.size()));
        if (buffer && count > 0) std::memcpy(buffer, report.data(), count);
        return count;
    }

    //强制重新导入指定资源，返回处理的源文件数
    int32 ORBEDEN_NATIVE_CALL ReimportManagedAsset(void* context, uint8* key, int32 length, uint8 prefix)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || !editor->HasProject() || editor->IsPlaying()) return 0;
        return static_cast<int32>(ResourceManager::Reimport(ReadUtf8(key, length), prefix != 0));
    }

    //强制重新导入全部已加载资源，返回处理的源文件数
    int32 ORBEDEN_NATIVE_CALL ReimportAllManagedAssets(void* context)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || !editor->HasProject() || editor->IsPlaying()) return 0;
        return static_cast<int32>(ResourceManager::Reimport(std::string(), true));
    }

    //读取编辑器已验证的单对象缓存及依赖，不重新解析原始复合资源。
    int32 ORBEDEN_NATIVE_CALL LoadCachedManagedAsset(void* context, const uint8* pathText, int32 pathLength,
        const uint8* keyText, int32 keyLength)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || !editor->HasProject()) return 0;
        std::filesystem::path path = Utf8Path::FromUtf8(ReadUtf8(pathText, pathLength));
        std::filesystem::path root = Utf8Path::FromUtf8(PathDefines::GetContentRoot()).parent_path() / "ResourceCache" / "Imported";
        std::error_code code;
        auto canonical = std::filesystem::weakly_canonical(path, code);
        if (code) return 0;
        auto relative = canonical.lexically_relative(std::filesystem::weakly_canonical(root, code));
        if (code || relative.empty() || relative.is_absolute()) return 0;
        for (const auto& part : relative) if (part == "..") return 0;
        std::string key = ReadUtf8(keyText, keyLength);
        std::string filename = Utf8Path::ToUtf8(canonical.filename());
        std::string blobName = CookedAssetSerializer::GetBlobFileName(key);
        if (!filename.ends_with(blobName) || filename.size() <= blobName.size()) return 0;
        std::string prefix = filename.substr(0, filename.size() - blobName.size());
        if (prefix.back() != '.') return 0;
        if (Object* loaded = ResourceManager::FindLoaded(key)) return loaded->GetObjectId();
        List<std::string> pending{ key };
        List<std::string> visited;
        List<std::string> created;
        for (usize index = 0; index < pending.size(); ++index)
        {
            std::string current = pending[index];
            if (ResourceManager::FindLoaded(current) || std::find(visited.begin(), visited.end(), current) != visited.end()) continue;
            visited.push_back(current);
            auto file = canonical.parent_path() / Utf8Path::FromUtf8(prefix + CookedAssetSerializer::GetBlobFileName(current));
            List<std::string> references;
            std::string error;
            if (!CookedAssetSerializer::Read(Utf8Path::ToUtf8(file), references, error, prefix))
            {
                Log::Error(error.c_str());
                for (const std::string& loadedKey : created) ResourceManager::Unload(loadedKey);
                return 0;
            }
            created.push_back(current);
            pending.insert(pending.end(), references.begin(), references.end());
        }
        Object* loaded = ResourceManager::FindLoaded(key);
        return loaded ? loaded->GetObjectId() : 0;
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

    //读取编辑 World 的未保存标记
    uint8 ORBEDEN_NATIVE_CALL IsManagedWorldDirty(void* context)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        return editor && editor->GetWorld().IsDirty() ? 1 : 0;
    }

    //标记编辑 World 有改动，供原生感知不到的托管字段写入使用
    void ORBEDEN_NATIVE_CALL SetManagedWorldDirty(void* context)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (editor && !editor->IsPlaying()) editor->GetWorld().SetDirty();
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
        for (TypeRuntimeId typeRuntimeId = 0; typeRuntimeId < Object::GetTypeCount(); ++typeRuntimeId)
        {
            Type* type = Object::FindType(typeRuntimeId);
            if (!type) continue;

            const List<Reflection::FieldInfo>& fields = type->GetFields();
            type->ForEachLiveObject([&](Object* object)
            {
                Script* host = object->Cast<Script>();
                if (host && host->IsManagedHost())
                {
                    List<ManagedScriptField> stored = host->GetManagedFields();
                    for (const ManagedScriptField& field : stored)
                    {
                        std::string mapped;
                        if (field.kind == Reflection::FieldKind::ObjectRef
                            && !field.value.starts_with("world://")
                            && TryMapResourceKey(field.value, oldKey, newKey, prefix != 0, mapped)
                            && host->SetManagedFieldValue(field.name, mapped)) ++changed;
                    }
                }
                for (const Reflection::FieldInfo& field : fields)
                {
                    bool isList = field.kind == Reflection::FieldKind::ObjectRefList;
                    if ((!isList && field.kind != Reflection::FieldKind::ObjectRef) || !field.getter || !field.setter) continue;

                    std::string current = field.GetValueAsString(object), mapped;
                    bool matched = isList
                        ? TryMapResourceKeyList(current, oldKey, newKey, prefix != 0, mapped)
                        : TryMapResourceKey(current, oldKey, newKey, prefix != 0, mapped);
                    if (!matched) continue;
                    if (field.SetValueFromString(object, mapped)) ++changed;
                }
            });
        }

        //已加载资源跟着新路径重登记：销毁它们会让场景里现有的引用全部悬空
        ResourceManager::RemapKeys(oldKey, newKey, prefix != 0);
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

    //一个类型的反射事实：按继承展开并过滤后的可见字段，加上自身与全部基类的名字。
    //两者都只随类型注册变化，所以共用一份按反射代次失效的缓存
    struct EditorComponentTypeFacts
    {
        List<const Reflection::FieldInfo*> fields;
        List<std::string> chain;
    };

    //收集一个类型的反射事实
    const EditorComponentTypeFacts& GetEditorComponentTypeFacts(Type* type)
    {
        static thread_local uint32 generation = 0;
        static thread_local std::unordered_map<TypeRuntimeId, EditorComponentTypeFacts> cache;
        static const EditorComponentTypeFacts empty;
        if (generation != Reflection::GetRegistryGeneration())
        {
            cache.clear();
            generation = Reflection::GetRegistryGeneration();
        }
        if (!type) return empty;
        auto [entry, added] = cache.try_emplace(type->GetId());
        if (added)
        {
            Reflection::CollectFields(type, entry->second.fields);
            std::erase_if(entry->second.fields, [](const Reflection::FieldInfo* field)
                { return !field || !field->name || !field->persistent || !field->getter || !field->setter; });
            //继承链也一并留下：判"是不是 Script"这类问题要沿链找名字，而链是随类型注册固定的
            for (Type* current = type; current; current = current->GetBaseType())
                entry->second.chain.emplace_back(current->GetName());
        }
        return entry->second;
    }

    //把引用列表字段的 Key 文本切成槽位；空段保留为空槽，便于按下标读改写
    List<std::string> SplitReferenceKeys(const std::string& text)
    {
        List<std::string> keys;
        usize start = 0;
        while (true)
        {
            usize separator = text.find(Reflection::ReferenceListSeparator, start);
            usize length = separator == std::string::npos ? std::string::npos : separator - start;
            keys.push_back(text.substr(start, length));
            if (separator == std::string::npos) return keys;
            start = separator + 1;
        }
    }

    //读取引用列表的槽位 Key；长度按字段报的真实槽位数对齐，空列表的文本切出来会多一个空段
    List<std::string> ReadReferenceKeys(const Reflection::FieldInfo* field, Component* component)
    {
        List<std::string> keys = SplitReferenceKeys(field->GetValueAsString(component));
        keys.resize(static_cast<usize>(field->GetListSize(component)));
        return keys;
    }

    //把槽位 Key 写回引用列表字段，空槽写成空段
    bool WriteReferenceKeys(const Reflection::FieldInfo* field, Component* component, const List<std::string>& keys)
    {
        std::string text;
        for (usize index = 0; index < keys.size(); ++index)
        {
            if (index > 0) text += Reflection::ReferenceListSeparator;
            text += keys[index];
        }
        return field->SetValueFromString(component, text);
    }

    //解析"字段名[下标]"形式的属性名
    bool ParseListElementName(const std::string& name, std::string& outFieldName, int32& outIndex)
    {
        usize open = name.rfind('[');
        if (open == std::string::npos || name.size() < open + 3 || name.back() != ']') return false;

        int64 index = 0;
        for (usize position = open + 1; position + 1 < name.size(); ++position)
        {
            char digit = name[position];
            if (digit < '0' || digit > '9') return false;
            index = index * 10 + (digit - '0');
            //槽位下标给个大而无当的上限：这条路径只服务编辑器 UI，别让畸形名字撑爆内存
            if (index > 65535) return false;
        }

        outFieldName = name.substr(0, open);
        outIndex = static_cast<int32>(index);
        return !outFieldName.empty();
    }

    //把精确 Script 识别为 C# 脚本宿主。
    Script* AsManagedScriptHost(Component* component)
    {
        Script* script = component ? component->Cast<Script>() : nullptr;
        return script && script->IsManagedHost() ? script : nullptr;
    }


    //跨域文本视图，仅在当前同步调用期间借用
    struct EditorTextAbi
    {
        const char* data = nullptr;
        int32 length = 0;
        int32 reserved = 0;
        EditorTextAbi() = default;
        explicit EditorTextAbi(std::string_view text) : data(text.data()), length(static_cast<int32>(text.size())) {}
    };

    struct EditorValueAbi
    {
        Reflection::ValueKind kind = Reflection::ValueKind::Empty;
        ScriptInterop::InteropStatus status = ScriptInterop::InteropStatus::Ok;
        uint64 payload[2]{};
    };

    struct EditorPropertyAbi
    {
        EditorTextAbi name;
        EditorTextAbi referenceType;
        EditorValueAbi value;
        Reflection::ValueKind declaredKind = Reflection::ValueKind::Empty;
        uint32 reserved = 0;
    };

    struct EditorComponentSnapshotAbi
    {
        EditorTextAbi stableId;
        const EditorPropertyAbi* properties = nullptr;
        int32 count = 0;
        uint32 generation = 0;
    };

    static_assert(sizeof(EditorTextAbi) == 16);
    static_assert(sizeof(EditorValueAbi) == 24);
    static_assert(sizeof(EditorPropertyAbi) == 64);
    static_assert(sizeof(EditorComponentSnapshotAbi) == 32);

    //读取内存中的类型化值
    template<typename T> T ReadEditorPayload(const EditorValueAbi& value)
    {
        T result{};
        static_assert(sizeof(T) <= sizeof(value.payload));
        std::memcpy(&result, value.payload, sizeof(T));
        return result;
    }

    //写入基础数值载荷
    template<typename T> void WriteEditorPayload(const Reflection::Value& source, EditorValueAbi& result)
    {
        T value{};
        if (!source.TryGet(value)) { result.status = ScriptInterop::InteropStatus::TypeMismatch; return; }
        static_assert(sizeof(T) <= sizeof(result.payload));
        std::memcpy(result.payload, &value, sizeof(T));
    }

    //映射 Inspector 的字段值类型
    Reflection::ValueKind GetEditorValueKind(Reflection::FieldKind kind, bool managed = false)
    {
        using F = Reflection::FieldKind;
        using V = Reflection::ValueKind;
        switch (kind)
        {
        case F::Bool: return V::Bool;
        case F::Int32: return V::Int32;
        case F::UInt32: return V::UInt32;
        case F::UInt64: return V::UInt64;
        case F::Float32: return V::Float32;
        case F::String: return V::String;
        case F::StringId: case F::ObjectRef: return V::StringId;
        case F::Vector3: return V::Vector3;
        case F::Color: return V::Color;
        case F::Quaternion: return V::Quaternion;
        case F::EnsId: return managed ? V::StringId : V::EnsId;
        default: return V::Empty;
        }
    }

    //编码类型化快照，文本保存在调用方的复用存储中
    void EncodeEditorValue(const Reflection::Value& source, EditorValueAbi& result, std::string& storage)
    {
        using V = Reflection::ValueKind;
        result.kind = source.GetKind();
        switch (result.kind)
        {
        case V::Bool: WriteEditorPayload<bool>(source, result); break;
        case V::Int32: WriteEditorPayload<int32>(source, result); break;
        case V::UInt32: WriteEditorPayload<uint32>(source, result); break;
        case V::UInt64: WriteEditorPayload<uint64>(source, result); break;
        case V::Float32: WriteEditorPayload<float32>(source, result); break;
        case V::Vector3: WriteEditorPayload<vector3>(source, result); break;
        case V::Color: WriteEditorPayload<color>(source, result); break;
        case V::Quaternion: WriteEditorPayload<quaternion>(source, result); break;
        case V::EnsId: WriteEditorPayload<EnsId>(source, result); break;
        case V::String: case V::StringId:
        {
            storage = source.ToString();
            EditorTextAbi text(storage);
            std::memcpy(result.payload, &text, sizeof(text));
            break;
        }
        default: result.status = ScriptInterop::InteropStatus::UnsupportedType; break;
        }
    }

    //解码托管侧写入的类型化值
    bool DecodeEditorValue(const EditorValueAbi& input, Reflection::Value& result)
    {
        using V = Reflection::ValueKind;
        switch (input.kind)
        {
        case V::Bool: result = Reflection::Value(ReadEditorPayload<bool>(input)); return true;
        case V::Int32: result = Reflection::Value(ReadEditorPayload<int32>(input)); return true;
        case V::UInt32: result = Reflection::Value(ReadEditorPayload<uint32>(input)); return true;
        case V::UInt64: result = Reflection::Value(ReadEditorPayload<uint64>(input)); return true;
        case V::Float32: result = Reflection::Value(ReadEditorPayload<float32>(input)); return true;
        case V::Vector3: result = Reflection::Value(ReadEditorPayload<vector3>(input)); return true;
        case V::Color: result = Reflection::Value(ReadEditorPayload<color>(input)); return true;
        case V::Quaternion: result = Reflection::Value(ReadEditorPayload<quaternion>(input)); return true;
        case V::EnsId: result = Reflection::Value(ReadEditorPayload<EnsId>(input)); return true;
        //容器条目写入的是槽位数
        case V::Array: result = Reflection::Value(ReadEditorPayload<int32>(input)); return true;
        case V::String: case V::StringId:
        {
            EditorTextAbi text = ReadEditorPayload<EditorTextAbi>(input);
            if (text.length < 0 || (text.length && !text.data)) return false;
            std::string value(text.data ? text.data : "", text.length);
            result = input.kind == V::StringId ? Reflection::Value(StringId(value)) : Reflection::Value(value);
            return true;
        }
        default: return false;
        }
    }

    //一次读取完整组件属性，返回缓冲区有效至下一次快照读取
    ScriptInterop::InteropStatus ORBEDEN_NATIVE_CALL ReadComponentSnapshot(void* context, int32 objectId, EditorComponentSnapshotAbi* output)
    {
        using S = ScriptInterop::InteropStatus;
        if (!output) return S::InvalidArgument;
        *output = {};
        Component* component = FindEditorComponent(context, objectId);
        if (!component) return S::NotFound;
        Script* host = AsManagedScriptHost(component);
        const List<const Reflection::FieldInfo*>& fields = GetEditorComponentTypeFacts(component->GetType()).fields;
        //预留按字段数估算，引用列表的元素条目会让它按需增长；容量跨帧复用，稳定后不再分配
        usize capacity = host ? host->GetManagedFields().size() + 1 : fields.size();
        static thread_local List<EditorPropertyAbi> properties;
        //条目文本用 deque：元素条目个数不定，push 时不会搬动已有字符串，借出去的指针一直有效
        static thread_local std::deque<std::string> texts;
        properties.clear();
        properties.reserve(capacity);
        texts.clear();

        //生成托管宿主字段快照
        if (host)
        {
            EditorPropertyAbi enabled;
            enabled.name = EditorTextAbi("enabled");
            enabled.declaredKind = Reflection::ValueKind::Bool;
            texts.emplace_back();
            EncodeEditorValue(Reflection::Value(host->GetEnabled()), enabled.value, texts.back());
            properties.push_back(enabled);
            for (const auto& field : host->GetManagedFields())
            {
                if (!field.inspectorVisible) continue;
                Reflection::ValueKind kind = GetEditorValueKind(field.kind, true);
                if (kind == Reflection::ValueKind::Empty) continue;
                EditorPropertyAbi entry;
                entry.name = EditorTextAbi(field.name);
                entry.declaredKind = kind;
                if (field.kind == Reflection::FieldKind::ObjectRef)
                {
                    texts.emplace_back(field.typeName);
                    std::string& reference = texts.back();
                    if (reference.starts_with("Ref<") && reference.ends_with(">")) reference = reference.substr(4, reference.size() - 5);
                    entry.referenceType = EditorTextAbi(reference);
                }
                if (field.kind == Reflection::FieldKind::EnsId) entry.referenceType = EditorTextAbi("EnsId");
                //传递原始文本，由托管快照在文本或声明类型变化时解析
                entry.value.kind = Reflection::ValueKind::String;
                texts.emplace_back(field.value);
                EditorTextAbi text(texts.back());
                std::memcpy(entry.value.payload, &text, sizeof(text));
                properties.push_back(entry);
            }
        }
        else
        {
            //直接读取原生类型化字段
            for (const auto* field : fields)
            {
                //引用列表展成一条容器条目加每个槽位一条元素条目。槽位数取字段自己报的真实长度：
                //列表文本分不出"0 个槽位"与"1 个空槽位"，两者都是空串
                if (field->kind == Reflection::FieldKind::ObjectRefList)
                {
                    int32 slotCount = field->GetListSize(component);
                    List<std::string> keys = ReadReferenceKeys(field, component);

                    EditorPropertyAbi container;
                    container.name = EditorTextAbi(field->name);
                    container.declaredKind = Reflection::ValueKind::Array;
                    if (field->objectRefTypeName) container.referenceType = EditorTextAbi(field->objectRefTypeName);
                    WriteEditorPayload<int32>(Reflection::Value(slotCount), container.value);
                    container.value.kind = Reflection::ValueKind::Array;
                    properties.push_back(container);

                    for (int32 slot = 0; slot < slotCount; ++slot)
                    {
                        EditorPropertyAbi element;
                        texts.emplace_back(std::string(field->name) + "[" + std::to_string(slot) + "]");
                        element.name = EditorTextAbi(texts.back());
                        element.declaredKind = Reflection::ValueKind::StringId;
                        if (field->objectRefTypeName) element.referenceType = EditorTextAbi(field->objectRefTypeName);
                        texts.emplace_back();
                        EncodeEditorValue(Reflection::Value(StringId(keys[static_cast<usize>(slot)])), element.value, texts.back());
                        properties.push_back(element);
                    }
                    continue;
                }

                Reflection::ValueKind kind = GetEditorValueKind(field->kind);
                if (kind == Reflection::ValueKind::Empty) continue;
                EditorPropertyAbi entry;
                entry.name = EditorTextAbi(field->name);
                entry.declaredKind = kind;
                if (field->kind == Reflection::FieldKind::EnsId) entry.referenceType = EditorTextAbi("EnsId");
                else if (field->kind == Reflection::FieldKind::ObjectRef && field->objectRefTypeName)
                    entry.referenceType = EditorTextAbi(field->objectRefTypeName);
                Reflection::Value value = field->kind == Reflection::FieldKind::ObjectRef
                    ? Reflection::Value(field->GetValueAsString(component)) : field->GetValue(component);
                texts.emplace_back();
                EncodeEditorValue(value, entry.value, texts.back());
                if (field->kind == Reflection::FieldKind::ObjectRef) entry.value.kind = kind;
                else if (entry.value.kind != kind) { entry.value.kind = kind; entry.value.status = S::TypeMismatch; }
                properties.push_back(entry);
            }
        }
        output->stableId = EditorTextAbi(component->GetInstanceId().GetPath());
        output->properties = properties.data();
        output->count = static_cast<int32>(properties.size());
        output->generation = Reflection::GetRegistryGeneration();
        return S::Ok;
    }

    //按字段名和精确值类型执行业务写入
    ScriptInterop::InteropStatus ORBEDEN_NATIVE_CALL SetComponentProperty(void* context, int32 objectId,
        const uint8* name, int32 length, const EditorValueAbi* input)
    {
        using S = ScriptInterop::InteropStatus;
        Component* component = FindEditorComponent(context, objectId);
        if (!component) return S::NotFound;
        if (!input || !name || length <= 0) return S::InvalidArgument;
        Reflection::Value value;
        if (!DecodeEditorValue(*input, value)) return S::TypeMismatch;
        std::string fieldName = ReadUtf8(name, length);
        if (Script* host = AsManagedScriptHost(component))
        {
            if (fieldName == "enabled")
            {
                bool enabled;
                if (!value.TryGet(enabled)) return S::TypeMismatch;
                host->SetEnabled(enabled);
                return S::Ok;
            }
            for (const auto& field : host->GetManagedFields())
            {
                if (field.name != fieldName || !field.inspectorVisible) continue;
                if (GetEditorValueKind(field.kind, true) != input->kind) return S::TypeMismatch;
                return host->SetManagedFieldValue(fieldName, value.ToString()) ? S::Ok : S::InvocationFailed;
            }
            return S::NotFound;
        }
        //引用列表：整份槽位 Key 文本读改写。"字段名"改槽位数（Array 值就是个数），"字段名[下标]"写单个槽位
        std::string elementFieldName;
        int32 elementIndex = -1;
        bool hasElementName = ParseListElementName(fieldName, elementFieldName, elementIndex);
        for (const auto* field : GetEditorComponentTypeFacts(component->GetType()).fields)
        {
            if (field->kind != Reflection::FieldKind::ObjectRefList) continue;

            bool resize = fieldName == field->name && input->kind == Reflection::ValueKind::Array;
            bool writeElement = hasElementName && elementFieldName == field->name
                && input->kind == Reflection::ValueKind::StringId;
            if (!resize && !writeElement) continue;

            List<std::string> keys = ReadReferenceKeys(field, component);
            if (resize)
            {
                int32 count = 0;
                if (!value.TryGet(count) || count < 0) return S::TypeMismatch;
                keys.resize(static_cast<usize>(count));
            }
            else
            {
                //越界只报找不到：槽位的增加由改长度那条路径负责，这里不偷偷加长
                if (elementIndex >= static_cast<int32>(keys.size())) return S::NotFound;
                keys[static_cast<usize>(elementIndex)] = value.ToString();
            }
            return WriteReferenceKeys(field, component, keys) ? S::Ok : S::InvocationFailed;
        }
        for (const auto* field : GetEditorComponentTypeFacts(component->GetType()).fields)
        {
            if (fieldName != field->name) continue;
            if (GetEditorValueKind(field->kind) != input->kind) return S::TypeMismatch;
            bool written = field->kind == Reflection::FieldKind::ObjectRef
                ? field->SetValueFromString(component, value.ToString()) : field->SetValue(component, value);
            return written ? S::Ok : S::InvocationFailed;
        }
        return S::NotFound;
    }

    //读取字段注册代次以刷新托管类型菜单
    uint32 ORBEDEN_NATIVE_CALL GetEditorRegistryGeneration(void*)
    {
        return Reflection::GetRegistryGeneration();
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
        Script* host = AsManagedScriptHost(component);
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

    //枚举符合声明类型的存活 Object 引用
    int32 ORBEDEN_NATIVE_CALL GetManagedReferenceObjects(void* context, const uint8* name, int32 length, uint8* buffer, int32 capacity)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor) return 0;
        std::string requested = ReadUtf8(name, length);
        if (requested.starts_with("Orbeden.")) requested.erase(0, 8);
        Type* expected = Object::FindType(requested);
        std::string result;
        for (TypeRuntimeId index = 0; index < Object::GetTypeCount(); ++index)
        {
            Type* type = Object::FindType(index);
            if (!type) continue;
            type->ForEachLiveObject([&](Object* object)
            {
                if (object->GetWorld() && object->GetWorld() != &editor->GetWorld()) return;
                Component* component = object->Cast<Component>();
                Script* host = AsManagedScriptHost(component);
                if (expected ? !object->Is(expected) : !host) return;
                Ens* ens = object->Cast<Ens>();
                if (!ens && component) ens = component->GetEns();
                if (ens && editor->GetEditorScene().IsTemporaryEns(ens->GetId())) return;
                const std::string& key = object->GetInstanceId().GetPath();
                std::string typeName = host ? host->GetManagedTypeName() : type->GetName();
                std::string label = ens ? ens->GetName() + " / " + typeName : key;
                EnsId owner = ens ? ens->GetId() : EnsId();
                result += std::to_string(object->GetObjectId()) + '\0' + key + '\0' + label + '\0'
                    + typeName + '\0' + std::to_string(owner.id) + '\0' + std::to_string(owner.version) + '\0';
            });
        }
        return CopyUtf8(result, buffer, capacity);
    }

    //读取场景引用的显示名称
    int32 ORBEDEN_NATIVE_CALL GetManagedReferenceLabel(void* context, const uint8* key, int32 length, uint8* buffer, int32 capacity)
    {
        Object* object = Object::FindObject(StringId(ReadUtf8(key, length)));
        if (!object) return 0;
        if (Ens* ens = object->Cast<Ens>()) return CopyUtf8(ens->GetName(), buffer, capacity);
        Component* component = object->Cast<Component>();
        if (!component || !component->GetEns()) return CopyUtf8(object->GetInstanceId().GetPath(), buffer, capacity);
        return CopyUtf8(component->GetEns()->GetName(), buffer, capacity);
    }

    //枚举当前 World 中可编辑的 Ens
    int32 ORBEDEN_NATIVE_CALL GetManagedWorldEns(void* context, EnsId* buffer, int32 capacity)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor) return 0;
        World& world = editor->GetWorld();
        editor->GetEditorScene().PruneSelection(world);
        int32 count = 0;
        auto visit = [&](auto&& self, Ens& ens) -> void
        {
            if (editor->GetEditorScene().IsTemporaryEns(ens.GetId())) return;
            if (buffer && count < capacity) buffer[count] = ens.GetId();
            ++count;
            for (EnsId child = ens.Transform()->firstChild; !child.IsNull(); child = world.GetTransform(child)->next)
                self(self, *world.GetEns(child));
        };
        world.ForEachEns([&](Ens& ens) { if (!ens.GetParent()) visit(visit, ens); });
        return count;
    }

    //选择、切换或清空层级对象选择
    void ORBEDEN_NATIVE_CALL SelectManagedEns(void* context, EnsId ens, uint8 toggle)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor) return;
        if (ens.IsNull()) editor->GetEditorScene().ClearSelection();
        else if (editor->GetWorld().IsAlive(ens) && !editor->GetEditorScene().IsTemporaryEns(ens))
        {
            if (toggle) editor->GetEditorScene().ToggleEns(ens);
            else editor->GetEditorScene().SelectEns(ens);
        }
    }

    //把编辑器相机聚焦到指定 Ens
    void ORBEDEN_NATIVE_CALL FocusManagedEns(void* context, EnsId ens)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || !editor->GetWorld().IsAlive(ens)) return;
        editor->GetEditorScene().FocusEns(editor->GetWorld(), ens);
        //相机在面板绘制阶段才被改，当帧已经渲染完，必须自己催下一帧
        editor->RequestRepaint();
    }

    //移动节点并按需保持世界变换
    uint8 ORBEDEN_NATIVE_CALL MoveManagedEns(void* context, EnsId child, EnsId parent, EnsId before, uint8 preserveWorld)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        if (!editor || editor->IsPlaying()) return 0;
        bool moved = editor->GetEditorScene().MoveEns(editor->GetWorld(), child, parent, before, preserveWorld != 0);
        if (moved) editor->RequestRepaint();
        return moved ? 1 : 0;
    }

    //匹配组件的声明类型或查询类型是否派生自 Component
    uint8 ORBEDEN_NATIVE_CALL MatchManagedComponentType(void* context, int32 objectId, const uint8* name, int32 length)
    {
        std::string requested = ReadUtf8(name, length);
        if (requested.starts_with("Orbeden.")) requested.erase(0, 8);
        Component* component = objectId != 0 ? FindEditorComponent(context, objectId) : nullptr;
        Type* type = objectId == 0 ? Object::FindType(requested) : component ? component->GetType() : nullptr;
        std::string target = objectId == 0 ? "Component" : requested;
        //沿缓存下来的继承链找名字，不再每次去跳一遍 GetBaseType()
        const List<std::string>& chain = GetEditorComponentTypeFacts(type).chain;
        return std::find(chain.begin(), chain.end(), target) != chain.end() ? 1 : 0;
    }

    //写入托管宿主的序列化字段
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
        Script* host = AsManagedScriptHost(FindEditorComponent(context, objectId));
        if (!host) return 0;
        std::string fieldType = ReadUtf8(typeName, typeNameLength);
        return host->SetManagedField(
            ReadUtf8(name, nameLength),
            fieldType,
            Script::GetManagedFieldKind(fieldType),
            ReadUtf8(value, valueLength),
            inspectorVisible != 0) ? 1 : 0;
    }

    //按类型注册顺序枚举可创建的原生组件类型。
    List<Type*> GetAddableNativeComponentTypes()
    {
        List<Type*> types;
        for (TypeRuntimeId typeRuntimeId = 0; typeRuntimeId < Object::GetTypeCount(); ++typeRuntimeId)
        {
            Type* type = Object::FindType(typeRuntimeId);
            if (!type || type == Component::StaticType() || type == Transform::StaticType() || type == Script::StaticType()) continue;
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
        int32 typeNameLength,
        uint8 isManaged)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        Ens* ens = editor ? editor->GetWorld().GetEns({ ensId, ensVersion }) : nullptr;
        if (!ens) return 0;

        std::string requestedType = ReadUtf8(typeName, typeNameLength);
        Type* type = Object::FindType(requestedType);
        Component* component = nullptr;
        if (!isManaged && type && type != Script::StaticType()
            && type->Is(Component::StaticType()) && type->CanCreateObject())
        {
            component = ens->AddComponentInstance(type);
        }
        else if (isManaged)
        {
            Component* createdHost = ens->AddComponentInstance(Script::StaticType());
            Script* host = createdHost ? createdHost->Cast<Script>() : nullptr;
            if (host && host->SetManagedTypeName(requestedType)) component = host;
            else if (host) ens->RemoveComponent(host);
        }
        return component ? component->GetObjectId() : 0;
    }

    uint8 ORBEDEN_NATIVE_CALL RemoveManagedNativeComponent(void* context, int32 objectId)
    {
        Component* component = FindEditorComponent(context, objectId);
        if (!component || component->GetType() == Transform::StaticType()) return 0;
        Ens* ens = component->GetEns();
        return ens && ens->RemoveComponent(component) ? 1 : 0;
    }

    int32 ORBEDEN_NATIVE_CALL CaptureManagedComponent(void* context, int32 objectId, uint8* buffer, int32 size)
    {
        return CopyUtf8(WorldSerializer::CaptureComponent(FindEditorComponent(context, objectId)), buffer, size);
    }

    int32 ORBEDEN_NATIVE_CALL FindManagedComponent(void* context, const uint8* key, int32 length)
    {
        Object* object = Object::FindObject(StringId(ReadUtf8(key, length)));
        Component* component = object ? FindEditorComponent(context, object->GetObjectId()) : nullptr;
        return component ? component->GetObjectId() : 0;
    }

    void* ORBEDEN_NATIVE_CALL GetEditorHostBinding(void* context, int32 objectId, void** pointer)
    {
        if (!pointer) return nullptr;
        Script* host = AsManagedScriptHost(FindEditorComponent(context, objectId));
        *pointer = host;
        if (!host) return nullptr;
        static ScriptBindApi api;
        api = ScriptBindApi::Create(host->GetWorld());
        return &api;
    }

    int32 ORBEDEN_NATIVE_CALL RestoreManagedComponent(void* context, uint32 id, uint32 version,
        const uint8* snapshot, int32 length, int32 index)
    {
        EditorSystem* editor = static_cast<EditorSystem*>(context);
        Ens* ens = editor ? editor->GetWorld().GetEns({ id, version }) : nullptr;
        Component* component = ens ? WorldSerializer::RestoreComponent(*ens, ReadUtf8(snapshot, length), index) : nullptr;
        return component ? component->GetObjectId() : 0;
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
        int32 order,
        uint8 fixedWorkspace,
        uint8 showBorder)
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
        info.showBorder = showBorder != 0;
        info.fixedWorkspace = fixedWorkspace != 0;
        return registration->panelManager->RegisterPanel(
            std::make_unique<ManagedPanelAdapter>(*registration->editor, std::move(info), handle)) ? 1 : 0;
    }

    //读取日志保留窗口的序号范围
    int32 ORBEDEN_NATIVE_CALL EditorLogGetRange(int64* oldestRevision, int64* newestRevision)
    {
        int64 oldest = 0;
        int64 newest = 0;
        int32 count = Log::GetRange(oldest, newest);
        if (oldestRevision) *oldestRevision = oldest;
        if (newestRevision) *newestRevision = newest;
        return count;
    }

    //按序号取出一条日志
    int32 ORBEDEN_NATIVE_CALL EditorLogCopyEntry(int64 revision, uint8* text, int32 capacity, int32* level, int64* timestampMilliseconds)
    {
        LogLevel value = LogLevel::Info;
        int64 timestamp = 0;
        int32 length = Log::CopyEntry(revision, reinterpret_cast<char*>(text), capacity, value, timestamp);
        if (level) *level = static_cast<int32>(value);
        if (timestampMilliseconds) *timestampMilliseconds = timestamp;
        return length;
    }

    //统计保留窗口内各级别的条数
    void ORBEDEN_NATIVE_CALL EditorLogGetCounts(int32* counts)
    {
        if (!counts) return;
        Log::GetCounts(counts);
    }

    //接收托管侧日志，托管侧已经输出过控制台，这里只入保留窗口
    void ORBEDEN_NATIVE_CALL EditorLogAppend(int32 level, const uint8* text, int32 length)
    {
        Log::Append(static_cast<LogLevel>(level), ReadUtf8(text, length).c_str());
    }

    //清空日志保留窗口
    void ORBEDEN_NATIVE_CALL EditorLogClear()
    {
        Log::Clear();
    }

    //开关按帧采集
    void ORBEDEN_NATIVE_CALL EditorProfilerSetCapturing(uint8 value)
    {
        Profiler::SetCapturing(value != 0);
    }

    //判断是否正在按帧采集
    uint8 ORBEDEN_NATIVE_CALL EditorProfilerIsCapturing()
    {
        return Profiler::IsCapturing() ? 1 : 0;
    }

    //清空帧历史
    void ORBEDEN_NATIVE_CALL EditorProfilerClearFrames()
    {
        Profiler::ClearFrames();
    }

    //读取帧历史的帧序号范围
    int32 ORBEDEN_NATIVE_CALL EditorProfilerGetFrameRange(int64* oldestFrame, int64* newestFrame)
    {
        int64 oldest = 0;
        int64 newest = 0;
        int32 count = Profiler::GetFrameRange(oldest, newest);
        if (oldestFrame) *oldestFrame = oldest;
        if (newestFrame) *newestFrame = newest;
        return count;
    }

    //拷贝帧摘要
    int32 ORBEDEN_NATIVE_CALL EditorProfilerCopyFrameSummaries(ProfileFrameSummary* output, int32 capacity)
    {
        return Profiler::CopyFrameSummaries(output, capacity);
    }

    //拷贝指定帧的采样事件
    int32 ORBEDEN_NATIVE_CALL EditorProfilerCopyFrameEvents(int64 frameIndex, ProfileEvent* output, int32 capacity)
    {
        return Profiler::CopyFrameEvents(frameIndex, output, capacity);
    }

    //读取名字驻留表的条数
    int32 ORBEDEN_NATIVE_CALL EditorProfilerGetNameCount()
    {
        return Profiler::GetNameCount();
    }

    //按编号拷贝采样名
    int32 ORBEDEN_NATIVE_CALL EditorProfilerCopyName(int32 nameId, uint8* text, int32 capacity)
    {
        return Profiler::CopyName(nameId, reinterpret_cast<char*>(text), capacity);
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

    std::filesystem::path managedDirectory = ExecutablePath::GetDirectory(executablePath) / "Managed";
    std::string editorAssemblyPath = Utf8Path::ToUtf8((managedDirectory / "Orbeden.Editor.dll").lexically_normal());

    //绑定托管入口并注册面板
    clrHost = &host;
    ManagedInitializeEditorFn initializeEditor = nullptr;
    ManagedGetInitializationErrorFn getInitializationError = nullptr;
    if (!clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorInitializeMethod,
        reinterpret_cast<void**>(&initializeEditor))
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorDrawPanelMethod, &DrawPanelFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorSetPanelVisibleMethod, &SetPanelVisibleFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorLoadGameAssemblyMethod, &LoadGameAssemblyFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorUnloadGameAssemblyMethod, &UnloadGameAssemblyFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorDrawSceneGizmosMethod, &DrawSceneGizmosFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorDrawStatusBarMethod, &DrawStatusBarFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorPublishGameAotMethod, &PublishGameAotFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorSaveProjectStateMethod, &SaveProjectStateFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorUndoMethod, &UndoFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorRedoMethod, &RedoFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorRequestRenameSelectedMethod, &RequestRenameSelectedFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorRequestDeleteSelectedMethod, &RequestDeleteSelectedFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorRequestReimportSelectedMethod, &RequestReimportSelectedFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorRequestReimportAllMethod, &RequestReimportAllFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorRequestCopySelectedMethod, &RequestCopySelectedFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorRequestPasteSelectedMethod, &RequestPasteSelectedFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorRequestToggleActiveSelectedMethod, &RequestToggleActiveSelectedFunction)
        || !clrHost->BindFunction(editorAssemblyPath, EditorTypeName, EditorGetInitializationErrorMethod, reinterpret_cast<void**>(&getInitializationError)))
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
    editorApi.application.getProjectText = reinterpret_cast<void*>(&GetManagedProjectText);
    editorApi.application.requestBuild = reinterpret_cast<void*>(&RequestManagedBuild);
    editorApi.application.getSelectedPlayerTarget = reinterpret_cast<void*>(&GetManagedPlayerTarget);
    editorApi.application.setSelectedPlayerTarget = reinterpret_cast<void*>(&SetManagedPlayerTarget);
    editorApi.application.mirrorTemplate = reinterpret_cast<void*>(&MirrorManagedTemplate);
    editorApi.application.isWorldDirty = reinterpret_cast<void*>(&IsManagedWorldDirty);
    editorApi.application.setWorldDirty = reinterpret_cast<void*>(&SetManagedWorldDirty);
    editorApi.gizmo = gizmoApi;
    editorApi.panels.context = &panelContext;
    editorApi.panels.registerPanel = reinterpret_cast<void*>(&RegisterManagedPanel);
    editorApi.assets.context = &editor;
    editorApi.assets.canModifyAssets = reinterpret_cast<void*>(&CanModifyManagedAssets);
    editorApi.assets.remapLiveReferences = reinterpret_cast<void*>(&RemapManagedLiveReferences);
    editorApi.assets.openWorld = reinterpret_cast<void*>(&OpenManagedWorld);
    editorApi.assets.getWorldKey = reinterpret_cast<void*>(&GetManagedWorldKey);
    editorApi.assets.saveWorld = reinterpret_cast<void*>(&SaveManagedWorld);
    editorApi.assets.setStartupWorld = reinterpret_cast<void*>(&SetManagedStartupWorld);
    editorApi.assets.createWorld = reinterpret_cast<void*>(&CreateManagedWorld);
    editorApi.assets.remapWorldKeys = reinterpret_cast<void*>(&RemapManagedWorldKeys);
    editorApi.assets.getProjectError = reinterpret_cast<void*>(&GetManagedProjectError);
    editorApi.assets.savePrefab = reinterpret_cast<void*>(&SaveManagedPrefab);
    editorApi.assets.instantiatePrefab = reinterpret_cast<void*>(&InstantiateManagedPrefab);
    editorApi.assets.captureEns = reinterpret_cast<void*>(&CaptureManagedEns);
    editorApi.assets.destroyEnsTree = reinterpret_cast<void*>(&DestroyManagedEnsTree);
    editorApi.assets.createEns = reinterpret_cast<void*>(&CreateManagedEns);
    editorApi.assets.reimportAsset = reinterpret_cast<void*>(&ReimportManagedAsset);
    editorApi.assets.reimportAllAssets = reinterpret_cast<void*>(&ReimportAllManagedAssets);
    editorApi.assets.loadCachedAsset = reinterpret_cast<void*>(&LoadCachedManagedAsset);
    editorApi.assets.saveMaterial = reinterpret_cast<void*>(&SaveManagedMaterial);
    editorApi.components.context = &editor;
    editorApi.components.readComponentSnapshot = reinterpret_cast<void*>(&ReadComponentSnapshot);
    editorApi.components.setComponentProperty = reinterpret_cast<void*>(&SetComponentProperty);
    editorApi.components.getRegistryGeneration = reinterpret_cast<void*>(&GetEditorRegistryGeneration);
    editorApi.components.getComponentCount = reinterpret_cast<void*>(&GetManagedComponentCount);
    editorApi.components.getComponentObjectId = reinterpret_cast<void*>(&GetManagedComponentObjectId);
    editorApi.components.getComponentTypeName = reinterpret_cast<void*>(&GetManagedComponentTypeName);
    editorApi.components.getComponentDomain = reinterpret_cast<void*>(&GetManagedComponentDomain);
    editorApi.components.setManagedField = reinterpret_cast<void*>(&SetManagedScriptField);
    editorApi.components.getAddableTypeCount = reinterpret_cast<void*>(&GetManagedAddableComponentTypeCount);
    editorApi.components.getAddableTypeName = reinterpret_cast<void*>(&GetManagedAddableComponentTypeName);
    editorApi.components.addComponent = reinterpret_cast<void*>(&AddManagedNativeComponent);
    editorApi.components.removeComponent = reinterpret_cast<void*>(&RemoveManagedNativeComponent);
    editorApi.components.captureComponent = reinterpret_cast<void*>(&CaptureManagedComponent);
    editorApi.components.restoreComponent = reinterpret_cast<void*>(&RestoreManagedComponent);
    editorApi.components.findComponent = reinterpret_cast<void*>(&FindManagedComponent);
    editorApi.components.getHostBinding = reinterpret_cast<void*>(&GetEditorHostBinding);
    editorApi.components.getWorldEns = reinterpret_cast<void*>(&GetManagedWorldEns);
    editorApi.components.selectEns = reinterpret_cast<void*>(&SelectManagedEns);
    editorApi.components.moveEns = reinterpret_cast<void*>(&MoveManagedEns);
    editorApi.components.matchComponentType = reinterpret_cast<void*>(&MatchManagedComponentType);
    editorApi.components.getReferenceObjects = reinterpret_cast<void*>(&GetManagedReferenceObjects);
    editorApi.components.getReferenceLabel = reinterpret_cast<void*>(&GetManagedReferenceLabel);
    editorApi.components.focusEns = reinterpret_cast<void*>(&FocusManagedEns);
    editorApi.log.getRange = reinterpret_cast<void*>(&EditorLogGetRange);
    editorApi.log.copyEntry = reinterpret_cast<void*>(&EditorLogCopyEntry);
    editorApi.log.getCounts = reinterpret_cast<void*>(&EditorLogGetCounts);
    editorApi.log.append = reinterpret_cast<void*>(&EditorLogAppend);
    editorApi.log.clear = reinterpret_cast<void*>(&EditorLogClear);
    editorApi.profiler.setCapturing = reinterpret_cast<void*>(&EditorProfilerSetCapturing);
    editorApi.profiler.isCapturing = reinterpret_cast<void*>(&EditorProfilerIsCapturing);
    editorApi.profiler.clearFrames = reinterpret_cast<void*>(&EditorProfilerClearFrames);
    editorApi.profiler.getFrameRange = reinterpret_cast<void*>(&EditorProfilerGetFrameRange);
    editorApi.profiler.copyFrameSummaries = reinterpret_cast<void*>(&EditorProfilerCopyFrameSummaries);
    editorApi.profiler.copyFrameEvents = reinterpret_cast<void*>(&EditorProfilerCopyFrameEvents);
    editorApi.profiler.getNameCount = reinterpret_cast<void*>(&EditorProfilerGetNameCount);
    editorApi.profiler.copyName = reinterpret_cast<void*>(&EditorProfilerCopyName);
    if (initializeEditor(&editorApi) == 0)
    {
        //托管侧没有日志通道，失败原因只能靠这个出口带回原生侧。
        int32 errorLength = 0;
        const void* errorText = getInitializationError ? getInitializationError(&errorLength) : nullptr;
        std::string reason = errorText && errorLength > 0
            ? std::string(static_cast<const char*>(errorText), static_cast<usize>(errorLength))
            : "no diagnostic was reported";
        Log::Error(("ManagedEditorBridge initialize failed: " + reason).c_str());
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
    DrawStatusBarFunction = nullptr;
    LoadGameAssemblyFunction = nullptr;
    UnloadGameAssemblyFunction = nullptr;
    PublishGameAotFunction = nullptr;
    SaveProjectStateFunction = nullptr;
    UndoFunction = nullptr;
    RedoFunction = nullptr;
    RequestRenameSelectedFunction = nullptr;
    RequestDeleteSelectedFunction = nullptr;
    RequestReimportSelectedFunction = nullptr;
    RequestReimportAllFunction = nullptr;
    RequestCopySelectedFunction = nullptr;
    RequestPasteSelectedFunction = nullptr;
    RequestToggleActiveSelectedFunction = nullptr;
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

void ManagedEditorBridge::DrawStatusBar()
{
    if (!initialized || !DrawStatusBarFunction) return;

    ManagedDrawEditorFn drawStatusBar = reinterpret_cast<ManagedDrawEditorFn>(DrawStatusBarFunction);
    drawStatusBar();
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

void ManagedEditorBridge::RequestRenameSelected()
{
    if (!initialized || !RequestRenameSelectedFunction) return;
    ManagedDrawEditorFn requestRename = reinterpret_cast<ManagedDrawEditorFn>(RequestRenameSelectedFunction);
    requestRename();
}

void ManagedEditorBridge::RequestDeleteSelected()
{
    if (!initialized || !RequestDeleteSelectedFunction) return;
    ManagedDrawEditorFn requestDelete = reinterpret_cast<ManagedDrawEditorFn>(RequestDeleteSelectedFunction);
    requestDelete();
}

void ManagedEditorBridge::RequestReimportSelected()
{
    if (!initialized || !RequestReimportSelectedFunction) return;
    ManagedDrawEditorFn requestReimport = reinterpret_cast<ManagedDrawEditorFn>(RequestReimportSelectedFunction);
    requestReimport();
}

void ManagedEditorBridge::RequestReimportAll()
{
    if (!initialized || !RequestReimportAllFunction) return;
    ManagedDrawEditorFn requestReimportAll = reinterpret_cast<ManagedDrawEditorFn>(RequestReimportAllFunction);
    requestReimportAll();
}

void ManagedEditorBridge::RequestCopySelected()
{
    if (!initialized || !RequestCopySelectedFunction) return;
    ManagedDrawEditorFn requestCopy = reinterpret_cast<ManagedDrawEditorFn>(RequestCopySelectedFunction);
    requestCopy();
}

void ManagedEditorBridge::RequestPasteSelected()
{
    if (!initialized || !RequestPasteSelectedFunction) return;
    ManagedDrawEditorFn requestPaste = reinterpret_cast<ManagedDrawEditorFn>(RequestPasteSelectedFunction);
    requestPaste();
}

void ManagedEditorBridge::RequestToggleActiveSelected()
{
    if (!initialized || !RequestToggleActiveSelectedFunction) return;
    ManagedDrawEditorFn requestToggleActive = reinterpret_cast<ManagedDrawEditorFn>(RequestToggleActiveSelectedFunction);
    requestToggleActive();
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
