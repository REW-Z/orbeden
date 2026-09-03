#pragma once

#include "Editor/EditorClrHost.h"
#include "Runtime/EnsId.h"

#include <string>

class EditorSystem;
class EditorGUI;
class PanelManager;
struct EditorGizmoApi;

//Editor 托管桥，负责 C++ 与 C# Editor Runtime 之间的调用。
class ManagedEditorBridge
{
private:
    EditorClrHost* clrHost = nullptr;
    void* DrawPanelFunction = nullptr;
    void* SetPanelVisibleFunction = nullptr;
    void* DrawSceneGizmosFunction = nullptr;
    void* LoadGameAssemblyFunction = nullptr;
    void* UnloadGameAssemblyFunction = nullptr;
    void* PublishGameAotFunction = nullptr;
    void* SaveProjectStateFunction = nullptr;
    void* UndoFunction = nullptr;
    void* RedoFunction = nullptr;
    void* WorldSavedFunction = nullptr;
    bool initialized = false;

public:
    //初始化 Editor 托管桥
    bool Initialize(EditorClrHost& host,
        EditorSystem& editor,
        EditorGUI& editorGUI,
        PanelManager& panelManager,
        const EditorGizmoApi& gizmoApi,
        const std::string& executablePath);

    //关闭 Editor 托管桥
    void Shutdown();

    //绘制一个 C# Editor Panel
    void DrawPanel(int32 handle,
        EnsId selectedEns,
        const EnsId* selectedEnsList,
        int32 selectedEnsCount,
        const std::string& selectedStableIds,
        const std::string& stableId);

    //设置 C# Editor Panel 可见状态
    void SetPanelVisible(int32 handle, bool visible);

    // 加载用户游戏程序集。
    void LoadGameAssembly(const std::string& assemblyPath, const std::string& sidecarPath);

    // 卸载用户游戏程序集引用。
    void UnloadGameAssembly();

    //绘制 C# Scene Handles。
    void DrawSceneGizmos();

    //保存托管 Editor 暂存的项目数据。
    bool SaveProjectState();

    //撤销最近一次托管属性或组件事务。
    bool Undo();

    //重做最近一次托管属性或组件事务。
    bool Redo();

    //通知托管 Editor 原生 World 已成功保存。
    void NotifyWorldSaved();

    // 使用 Editor C# 发布用户游戏 NativeAOT 库。
    bool PublishGameAot(const std::string& repositoryRoot,
        const std::string& projectRoot,
        const std::string& scriptProject,
        const std::string& configuration,
        const std::string& targetPlatform,
        std::string& error);

    //判断 Editor 托管桥是否可用
    bool IsInitialized() const;
};
