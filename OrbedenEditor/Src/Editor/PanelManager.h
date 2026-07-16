#pragma once

#include "Editor/EditorLayoutState.h"
#include "Editor/Panels/IEditorPanel.h"
#include "Runtime/EngineTypes.h"

#include <memory>
#include <string>

//编辑器浮动面板管理器。
class PanelManager
{
private:
    struct PanelEntry
    {
    public:
        EditorPanelInfo info;
        std::unique_ptr<IEditorPanel> panel;
        bool visible = false;
        bool hasPosition = false;
        bool hasSize = false;
        bool applyPosition = false;
        bool applySize = false;
        vector2 position = { 0.0f, 0.0f };
        vector2 size = { 0.0f, 0.0f };
        int32 dockNode = -1;
        bool moving = false;
        vector2 moveOffset = { 0.0f, 0.0f };
    };

    struct DockNode
    {
    public:
        int32 id = 0;
        int32 firstChild = -1;
        int32 secondChild = -1;
        bool vertical = true;
        float32 ratio = 0.5f;
        bool workspace = false;
        List<std::string> tabs;
        std::string activePanel;
    };

    struct PendingDockCommand
    {
    public:
        bool pending = false;
        std::string panelId;
        int32 targetNode = -1;
        PanelDockPlacement placement = PanelDockPlacement::Center;
    };

    struct PendingFloatCommand
    {
    public:
        bool pending = false;
        std::string panelId;
        vector2 position = { 0.0f, 0.0f };
    };

    List<PanelEntry> panels;
    List<DockNode> dockNodes;
    int32 dockRoot = -1;
    int32 nextDockNodeId = 1;
    bool defaultLayoutPending = true;
    std::string draggedPanel;
    PendingDockCommand pendingDock;
    PendingFloatCommand pendingFloat;
    std::string pendingClosePanel;
    bool tabMergeTargetHovered = false;
    bool workspaceHovered = false;
    bool workspaceRectValid = false;
    vector2 workspacePosition = { 0.0f, 0.0f };
    vector2 workspaceSize = { 0.0f, 0.0f };

public:
    //注册一个面板实例
    bool RegisterPanel(std::unique_ptr<IEditorPanel> panel);

    //绘制 Views 菜单内容
    void DrawViewsMenu();

    //绘制所有可见面板
    void DrawPanels();

    //恢复内置默认停靠布局
    void ResetDockLayout();

    //应用项目中保存的面板布局
    void ApplyLayout(const EditorLayoutState& layout);

    //写出当前面板布局
    void WriteLayout(EditorLayoutState& layout) const;

    //判断面板是否可见
    bool IsPanelVisible(const char* id) const;

    //设置面板可见状态
    void SetPanelVisible(const char* id, bool visible);

    //判断鼠标是否位于中央编辑器工作区
    bool IsMouseOverWorkspace() const;

    /// <summary>获取当前帧中央编辑器工作区矩形。</summary>
    bool TryGetWorkspaceRect(vector2& position, vector2& size) const;

private:
    PanelEntry* FindPanel(const char* id);
    const PanelEntry* FindPanel(const char* id) const;
    void ApplyVisibility(PanelEntry& entry, bool visible);
    void ClampPanel(PanelEntry& entry) const;
    bool ClampPanelRect(vector2& position, vector2& size) const;
    DockNode* FindDockNode(int32 id);
    const DockNode* FindDockNode(int32 id) const;
    DockNode& CreateDockNode();
    void BuildDefaultDockLayout();
    void DrawDockHost();
    void DrawRootDockTarget(const vector2& position, const vector2& size);
    void DrawDockNode(int32 nodeId, const vector2& position, const vector2& size);
    void DrawDockLeaf(DockNode& node, const vector2& position, const vector2& size);
    PanelDockPlacement GetDockPlacement(const vector2& position, const vector2& size, float32 edgeRatio) const;
    void DrawDockPreview(const vector2& position, const vector2& size, PanelDockPlacement placement) const;
    bool IsRootDockPlacement(PanelDockPlacement placement) const;
    void ApplyPendingCommands();
    void DrawFloatingPanel(PanelEntry& entry);
    void DockPanel(const std::string& panelId, int32 targetNode, PanelDockPlacement placement);
    void RemovePanelFromDock(const std::string& panelId);
    void CompactDockNode(int32 nodeId);
    int32 FindDockParent(int32 nodeId) const;
    void SynchronizeDockAssignments();
    int32 FindBestDockTarget(int32 nodeId) const;
};
