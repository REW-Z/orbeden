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
    void* DrawStatusBarFunction = nullptr;
    void* LoadGameAssemblyFunction = nullptr;
    void* UnloadGameAssemblyFunction = nullptr;
    void* PublishGameAotFunction = nullptr;
    void* SaveProjectStateFunction = nullptr;
    void* UndoFunction = nullptr;
    void* RedoFunction = nullptr;
    void* RequestRenameSelectedFunction = nullptr;
    void* RequestDeleteSelectedFunction = nullptr;
    void* RequestReimportSelectedFunction = nullptr;
    void* RequestReimportAllFunction = nullptr;
    void* RequestCopySelectedFunction = nullptr;
    void* RequestPasteSelectedFunction = nullptr;
    void* RequestToggleActiveSelectedFunction = nullptr;
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
    void LoadGameAssembly(const std::string& assemblyPath);

    // 卸载用户游戏程序集引用。
    void UnloadGameAssembly();

    //绘制 C# Scene Handles。
    void DrawSceneGizmos();

    //绘制底部状态栏内容。
    void DrawStatusBar();

    //保存托管 Editor 暂存的项目数据。
    bool SaveProjectState();

    //撤销最近一次托管属性或组件事务。
    bool Undo();

    //重做最近一次托管属性或组件事务。
    bool Redo();

    //请求当前聚焦的面板开始重命名选中项。
    void RequestRenameSelected();

    //请求当前聚焦的面板删除选中项。
    void RequestDeleteSelected();

    //请求当前聚焦的面板重新导入选中资源。
    void RequestReimportSelected();

    //请求重新导入全部已加载资源。
    void RequestReimportAll();

    //请求当前聚焦的面板复制选中项。
    void RequestCopySelected();

    //请求当前聚焦的面板粘贴剪贴板内容。
    void RequestPasteSelected();

    //请求当前聚焦的面板切换选中项的激活状态。
    void RequestToggleActiveSelected();

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
