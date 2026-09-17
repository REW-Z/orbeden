#pragma once

#include "Editor/EditorFloatingWindow.h"
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
        int32 returnDockNode = -1;
        bool moving = false;
        vector2 moveOffset = { 0.0f, 0.0f };

        //非空表示承载在独立 GLFW 窗口中
        std::unique_ptr<EditorFloatingWindow> osWindow;
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
        bool independent = false;
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
    bool repaintPending = false;

    //本帧停靠区矩形，供面板判断自己是否贴着外圈
    vector2 dockAreaPosition = { 0.0f, 0.0f };
    vector2 dockAreaSize = { 0.0f, 0.0f };

    //本帧各面板的绘制矩形：每个面板一步画成完整的圆角矩形，统一由宿主绘制
    struct PanelFrame
    {
    public:
        vector2 min = { 0.0f, 0.0f };
        vector2 max = { 0.0f, 0.0f };
        bool opaque = true;

        //是否绘制 1px 边框，由当前显示的面板决定
        bool showBorder = true;
    };
    List<PanelFrame> framePanels;

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

    //隐藏全部面板（进入 Play 前调用，布局恢复由调用方负责）
    void HideAllPanels();

    //销毁全部独立窗口（主窗口退出与主 ImGui 上下文销毁前调用）
    void DestroyFloatingOsWindows();

    //获取并清除面板管理器的重绘请求
    bool TakeRepaintRequest();

    //判断独立窗口中是否有控件处于活动状态
    bool IsAnyFloatingItemActive() const;

private:
    PanelEntry* FindPanel(const char* id);
    const PanelEntry* FindPanel(const char* id) const;
    void ApplyVisibility(PanelEntry& entry, bool visible);
    void ClampPanel(PanelEntry& entry) const;
    bool ClampPanelRect(vector2& position, vector2& size) const;
    DockNode* FindDockNode(int32 id);
    const DockNode* FindDockNode(int32 id) const;
    DockNode& CreateDockNode();
    bool NodeHostsFixedPanel(const DockNode& node) const;
    void BuildDefaultDockLayout();
    void DrawDockHost();
    void DrawRootDockTarget(const vector2& position, const vector2& size);
    void DrawDockNode(int32 nodeId, const vector2& position, const vector2& size,
        const vector2& visualMin, const vector2& visualMax);
    void DrawDockLeaf(DockNode& node, const vector2& position, const vector2& size,
        const vector2& visualMin, const vector2& visualMax);
    PanelDockPlacement GetDockPlacement(const vector2& position, const vector2& size, float32 edgeRatio) const;
    void DrawDockPreview(const vector2& position, const vector2& size, PanelDockPlacement placement) const;
    bool IsRootDockPlacement(PanelDockPlacement placement) const;
    void ApplyPendingCommands();
    void ClearPendingCommands();
    void DrawFloatingPanel(PanelEntry& entry);
    void DrawFloatingOsPanel(PanelEntry& entry);
    void CreateFloatingOsWindow(PanelEntry& entry);
    void DestroyFloatingOsWindow(PanelEntry& entry);
    bool ClampScreenRect(vector2& position, vector2& size) const;
    void DockPanel(const std::string& panelId, int32 targetNode, PanelDockPlacement placement);
    void RemovePanelFromDock(const std::string& panelId);
    void CompactDockNode(int32 nodeId);
    int32 FindDockParent(int32 nodeId) const;
    void SynchronizeDockAssignments();
    int32 FindBestDockTarget(int32 nodeId) const;
};
