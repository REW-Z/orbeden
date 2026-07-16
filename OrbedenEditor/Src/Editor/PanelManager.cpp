#include "Editor/PanelManager.h"

#include "Log/Log.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace
{
    constexpr float32 MinPanelWidth = 120.0f;
    constexpr float32 MinPanelHeight = 80.0f;
    constexpr float32 DockTabHeight = 25.0f;
    constexpr float32 DockSplitterSize = 5.0f;
    constexpr const char* PanelDragPayload = "ORBEDEN_PANEL";

    //转换为 ImGui 二维向量。
    ImVec2 ToImVec2(const vector2& value)
    {
        return ImVec2(value.x, value.y);
    }

    //转换为引擎二维向量。
    vector2 ToVector2(const ImVec2& value)
    {
        return vector2 { value.x, value.y };
    }
}

//注册一个面板实例
bool PanelManager::RegisterPanel(std::unique_ptr<IEditorPanel> panel)
{
    if (!panel)
    {
        Log::Error("Panel registration failed: panel is null.");
        return false;
    }

    const EditorPanelInfo& info = panel->GetPanelInfo();
    if (info.id.empty())
    {
        Log::Error("Panel registration failed: panel id is empty.");
        return false;
    }
    if (FindPanel(info.id.c_str()))
    {
        std::string error = "Panel registration failed: duplicate panel id " + info.id + ".";
        Log::Error(error.c_str());
        return false;
    }

    PanelEntry entry;
    entry.info = info;
    if (entry.info.title.empty()) entry.info.title = entry.info.id;
    entry.panel = std::move(panel);
    entry.visible = info.defaultVisible;
    entry.size = info.defaultSize;
    panels.push_back(std::move(entry));
    std::stable_sort(panels.begin(), panels.end(), [](const PanelEntry& left, const PanelEntry& right)
    {
        if (left.info.order != right.info.order) return left.info.order < right.info.order;
        return left.info.id < right.info.id;
    });
    defaultLayoutPending = true;
    return true;
}

//绘制 Views 菜单内容
void PanelManager::DrawViewsMenu()
{
    if (panels.empty())
    {
        ImGui::TextUnformatted("No panels.");
        return;
    }

    for (PanelEntry& entry : panels)
    {
        bool visible = entry.visible;
        if (ImGui::Checkbox(entry.info.title.c_str(), &visible))
        {
            SetPanelVisible(entry.info.id.c_str(), visible);
        }
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Reset Dock Layout"))
    {
        ResetDockLayout();
    }
}

//绘制所有可见面板
void PanelManager::DrawPanels()
{
    workspaceHovered = false;
    workspaceRectValid = false;
    if (defaultLayoutPending)
    {
        BuildDefaultDockLayout();
    }

    DrawDockHost();
    ApplyPendingCommands();
    for (PanelEntry& entry : panels)
    {
        if (entry.visible && entry.panel && entry.dockNode < 0) DrawFloatingPanel(entry);
    }
}

//恢复内置默认停靠布局
void PanelManager::ResetDockLayout()
{
    dockNodes.clear();
    dockRoot = -1;
    nextDockNodeId = 1;
    defaultLayoutPending = true;
    for (PanelEntry& entry : panels)
    {
        entry.dockNode = -1;
        entry.applyPosition = false;
        entry.applySize = false;
    }
}

//应用项目中保存的面板布局
void PanelManager::ApplyLayout(const EditorLayoutState& layout)
{
    dockNodes.clear();
    dockRoot = layout.dockRoot;
    nextDockNodeId = 1;
    for (const EditorDockNodeState& state : layout.dockNodes)
    {
        DockNode node;
        node.id = state.id;
        node.firstChild = state.firstChild;
        node.secondChild = state.secondChild;
        node.vertical = state.vertical;
        node.ratio = std::clamp(state.ratio, 0.1f, 0.9f);
        node.workspace = state.workspace;
        node.activePanel = state.activePanel;
        dockNodes.push_back(node);
        nextDockNodeId = std::max(nextDockNodeId, state.id + 1);
    }

    for (const EditorPanelState& state : layout.panels)
    {
        PanelEntry* entry = FindPanel(state.id.c_str());
        if (!entry) continue;

        ApplyVisibility(*entry, state.visible);
        entry->dockNode = state.visible && FindDockNode(state.dockNode) ? state.dockNode : -1;
        if (DockNode* node = FindDockNode(entry->dockNode))
        {
            node->tabs.push_back(entry->info.id);
        }
        if (state.hasPosition)
        {
            entry->hasPosition = true;
            entry->position = state.position;
            entry->applyPosition = true;
        }
        if (state.hasSize && state.size.x > 0.0f && state.size.y > 0.0f)
        {
            entry->hasSize = true;
            entry->size = state.size;
            entry->applySize = true;
        }
    }

    defaultLayoutPending = dockNodes.empty() || !FindDockNode(dockRoot);
    if (defaultLayoutPending)
    {
        for (PanelEntry& entry : panels) entry.dockNode = -1;
    }
    else
    {
        SynchronizeDockAssignments();
        bool hasWorkspace = std::any_of(dockNodes.begin(), dockNodes.end(), [](const DockNode& node)
            { return node.workspace; });
        if (!hasWorkspace)
        {
            int32 candidateId = FindBestDockTarget(dockRoot);
            DockNode* candidate = FindDockNode(candidateId);
            if (candidate && candidate->tabs.empty()) candidate->workspace = true;
        }
    }
}

//写出当前面板布局
void PanelManager::WriteLayout(EditorLayoutState& layout) const
{
    layout.panels.clear();
    layout.dockNodes.clear();
    layout.dockRoot = dockRoot;
    for (const PanelEntry& entry : panels)
    {
        EditorPanelState state;
        state.id = entry.info.id;
        state.visible = entry.visible;
        state.hasPosition = entry.hasPosition;
        state.hasSize = entry.hasSize;
        state.position = entry.position;
        state.size = entry.hasSize ? entry.size : entry.info.defaultSize;
        state.dockNode = entry.dockNode;
        layout.panels.push_back(state);
    }
    for (const DockNode& node : dockNodes)
    {
        EditorDockNodeState state;
        state.id = node.id;
        state.firstChild = node.firstChild;
        state.secondChild = node.secondChild;
        state.vertical = node.vertical;
        state.ratio = node.ratio;
        state.workspace = node.workspace;
        state.activePanel = node.activePanel;
        layout.dockNodes.push_back(state);
    }
}

//判断面板是否可见
bool PanelManager::IsPanelVisible(const char* id) const
{
    const PanelEntry* entry = FindPanel(id);
    return entry && entry->visible;
}

//设置面板可见状态
void PanelManager::SetPanelVisible(const char* id, bool visible)
{
    PanelEntry* entry = FindPanel(id);
    if (!entry) return;

    if (!visible && entry->dockNode >= 0)
    {
        int32 oldNode = entry->dockNode;
        RemovePanelFromDock(entry->info.id);
        CompactDockNode(oldNode);
    }
    ApplyVisibility(*entry, visible);
    if (visible && entry->dockNode < 0 && FindDockNode(dockRoot))
    {
        DockPanel(entry->info.id, dockRoot, PanelDockPlacement::Center);
    }
}

//判断鼠标是否位于中央编辑器工作区
bool PanelManager::IsMouseOverWorkspace() const
{
    return workspaceHovered;
}

//获取当前帧中央编辑器工作区矩形
bool PanelManager::TryGetWorkspaceRect(vector2& position, vector2& size) const
{
    if (!workspaceRectValid) return false;

    position = workspacePosition;
    size = workspaceSize;
    return true;
}

PanelManager::PanelEntry* PanelManager::FindPanel(const char* id)
{
    if (!id) return nullptr;

    for (PanelEntry& entry : panels)
    {
        if (entry.info.id == id) return &entry;
    }

    return nullptr;
}

const PanelManager::PanelEntry* PanelManager::FindPanel(const char* id) const
{
    return const_cast<PanelManager*>(this)->FindPanel(id);
}

//应用面板可见性。
void PanelManager::ApplyVisibility(PanelEntry& entry, bool visible)
{
    if (entry.visible == visible) return;

    entry.visible = visible;
    if (!entry.panel) return;

    if (entry.visible)
    {
        entry.panel->OnPanelShown();
    }
    else
    {
        entry.panel->OnPanelHidden();
    }
}

//限制面板停留在主窗口内。
void PanelManager::ClampPanel(PanelEntry& entry) const
{
    if (!entry.hasPosition && !entry.hasSize) return;

    vector2 size = entry.hasSize ? entry.size : entry.info.defaultSize;
    vector2 position = entry.position;
    bool clamped = ClampPanelRect(position, size);
    if (!clamped) return;

    if (entry.hasPosition)
    {
        entry.position = position;
    }
    if (entry.hasSize)
    {
        entry.size = size;
    }
}

bool PanelManager::ClampPanelRect(vector2& position, vector2& size) const
{
    if (!ImGui::GetCurrentContext() || ImGui::GetFrameCount() <= 0) return false;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return false;

    vector2 oldPosition = position;
    vector2 oldSize = size;
    float32 maxWidth = std::max(1.0f, viewport->WorkSize.x);
    float32 maxHeight = std::max(1.0f, viewport->WorkSize.y);
    size.x = std::clamp(size.x, std::min(MinPanelWidth, maxWidth), maxWidth);
    size.y = std::clamp(size.y, std::min(MinPanelHeight, maxHeight), maxHeight);

    float32 minX = viewport->WorkPos.x;
    float32 minY = viewport->WorkPos.y;
    float32 maxX = viewport->WorkPos.x + std::max(0.0f, viewport->WorkSize.x - size.x);
    float32 maxY = viewport->WorkPos.y + std::max(0.0f, viewport->WorkSize.y - size.y);
    position.x = std::clamp(position.x, minX, maxX);
    position.y = std::clamp(position.y, minY, maxY);

    return position.x != oldPosition.x
        || position.y != oldPosition.y
        || size.x != oldSize.x
        || size.y != oldSize.y;
}

PanelManager::DockNode* PanelManager::FindDockNode(int32 id)
{
    for (DockNode& node : dockNodes)
    {
        if (node.id == id) return &node;
    }
    return nullptr;
}

const PanelManager::DockNode* PanelManager::FindDockNode(int32 id) const
{
    return const_cast<PanelManager*>(this)->FindDockNode(id);
}

PanelManager::DockNode& PanelManager::CreateDockNode()
{
    DockNode node;
    node.id = nextDockNodeId++;
    dockNodes.push_back(node);
    return dockNodes.back();
}

//建立类似 Unity Editor 的默认布局，并保留中央场景区域。
void PanelManager::BuildDefaultDockLayout()
{
    dockNodes.clear();
    nextDockNodeId = 1;
    dockRoot = CreateDockNode().id;

    for (PanelEntry& entry : panels) entry.dockNode = -1;

    int32 centerNode = dockRoot;
    const PanelDockPlacement sideOrder[] = {
        PanelDockPlacement::Left,
        PanelDockPlacement::Right,
        PanelDockPlacement::Top,
        PanelDockPlacement::Bottom
    };
    for (PanelDockPlacement placement : sideOrder)
    {
        for (PanelEntry& entry : panels)
        {
            if (entry.info.defaultDock != placement) continue;

            int32 splitNode = centerNode;
            DockPanel(entry.info.id, splitNode, placement);
            const DockNode* split = FindDockNode(splitNode);
            if (!split) continue;
            centerNode = split->firstChild == entry.dockNode ? split->secondChild : split->firstChild;
        }
    }

    for (PanelEntry& entry : panels)
    {
        if (entry.info.defaultDock == PanelDockPlacement::Center)
        {
            DockPanel(entry.info.id, centerNode, PanelDockPlacement::Center);
        }
    }

    if (DockNode* workspace = FindDockNode(centerNode)) workspace->workspace = true;

    defaultLayoutPending = false;
    SynchronizeDockAssignments();
}

//绘制覆盖主视口工作区的透明停靠宿主。
void PanelManager::DrawDockHost()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport || !FindDockNode(dockRoot)) return;

    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##EditorDockHost", nullptr, flags))
    {
        vector2 position = ToVector2(viewport->WorkPos);
        vector2 size = ToVector2(viewport->WorkSize);
        tabMergeTargetHovered = false;
        DrawDockNode(dockRoot, position, size);
        DrawRootDockTarget(position, size);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

//处理整个工作区外圈的停靠目标。
void PanelManager::DrawRootDockTarget(const vector2& position, const vector2& size)
{
    if (tabMergeTargetHovered) return;

    const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
    if (!activePayload || !activePayload->IsDataType(PanelDragPayload)) return;

    PanelDockPlacement placement = GetDockPlacement(position, size, 0.12f);
    if (!IsRootDockPlacement(placement)) return;

    ImGui::SetCursorScreenPos(ToImVec2(position));
    ImGui::InvisibleButton("##RootDockTarget", ToImVec2(size));
    if (!ImGui::BeginDragDropTarget()) return;

    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PanelDragPayload,
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
    if (payload)
    {
        DrawDockPreview(position, size, placement);
        if (payload->IsDelivery())
        {
            pendingDock.pending = true;
            pendingDock.panelId = static_cast<const char*>(payload->Data);
            pendingDock.targetNode = dockRoot;
            pendingDock.placement = placement;
            draggedPanel.clear();
        }
    }
    ImGui::EndDragDropTarget();
}

//递归绘制分割节点和叶子面板组。
void PanelManager::DrawDockNode(int32 nodeId, const vector2& position, const vector2& size)
{
    DockNode* node = FindDockNode(nodeId);
    if (!node) return;
    if (node->firstChild < 0 || node->secondChild < 0)
    {
        DrawDockLeaf(*node, position, size);
        return;
    }

    float32 total = node->vertical ? size.x : size.y;
    float32 firstLength = std::max(0.0f, (total - DockSplitterSize) * node->ratio);
    vector2 firstSize = size;
    vector2 secondPosition = position;
    vector2 secondSize = size;
    if (node->vertical)
    {
        firstSize.x = firstLength;
        secondPosition.x += firstLength + DockSplitterSize;
        secondSize.x = std::max(0.0f, total - firstLength - DockSplitterSize);
    }
    else
    {
        firstSize.y = firstLength;
        secondPosition.y += firstLength + DockSplitterSize;
        secondSize.y = std::max(0.0f, total - firstLength - DockSplitterSize);
    }

    int32 firstChild = node->firstChild;
    int32 secondChild = node->secondChild;
    DrawDockNode(firstChild, position, firstSize);
    DrawDockNode(secondChild, secondPosition, secondSize);

    std::string splitterId = "##DockSplitter" + std::to_string(nodeId);
    ImGui::SetCursorScreenPos(node->vertical
        ? ImVec2(position.x + firstLength, position.y)
        : ImVec2(position.x, position.y + firstLength));
    ImGui::InvisibleButton(splitterId.c_str(), node->vertical
        ? ImVec2(DockSplitterSize, size.y)
        : ImVec2(size.x, DockSplitterSize));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        ImGui::SetMouseCursor(node->vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui::IsItemActive() && total > 1.0f)
    {
        float32 mouse = node->vertical ? ImGui::GetIO().MousePos.x - position.x : ImGui::GetIO().MousePos.y - position.y;
        node->ratio = std::clamp(mouse / total, 0.1f, 0.9f);
    }
}

//绘制一个带标签页的停靠叶子。
void PanelManager::DrawDockLeaf(DockNode& node, const vector2& position, const vector2& size)
{
    if (node.workspace)
    {
        workspacePosition = position;
        workspaceSize = size;
        workspaceRectValid = size.x >= 1.0f && size.y >= 1.0f;
    }
    if (size.x < 1.0f || size.y < 1.0f) return;

    ImGui::SetCursorScreenPos(ToImVec2(position));
    std::string childId = "##DockLeaf" + std::to_string(node.id);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, node.tabs.empty() ? 0.0f : 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    if (!node.tabs.empty()) ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    bool open = ImGui::BeginChild(childId.c_str(), ToImVec2(size), node.tabs.empty() ? ImGuiChildFlags_None : ImGuiChildFlags_Borders,
        node.tabs.empty() ? ImGuiWindowFlags_NoBackground : ImGuiWindowFlags_None);
    if (node.workspace && node.tabs.empty()
        && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        workspaceHovered = true;
    }

    std::string closePanel;
    bool floatActive = false;
    if (!node.tabs.empty())
    {
        PanelEntry* activeEntry = FindPanel(node.activePanel.c_str());
        if (!activeEntry || !activeEntry->visible)
        {
            node.activePanel.clear();
            for (const std::string& panelId : node.tabs)
            {
                PanelEntry* candidate = FindPanel(panelId.c_str());
                if (candidate && candidate->visible)
                {
                    node.activePanel = panelId;
                    break;
                }
            }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 2.0f));
        bool drewTab = false;
        for (std::size_t index = 0; index < node.tabs.size(); index++)
        {
            PanelEntry* entry = FindPanel(node.tabs[index].c_str());
            if (!entry || !entry->visible) continue;
            if (drewTab) ImGui::SameLine();

            bool selected = node.activePanel == entry->info.id;
            std::string tabLabel = entry->info.title + "###DockTab" + entry->info.id;
            float32 tabWidth = ImGui::CalcTextSize(entry->info.title.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f + 10.0f;
            if (selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabHovered));
            }
            bool tabClicked = ImGui::Button(tabLabel.c_str(), ImVec2(tabWidth, DockTabHeight));
            if (selected) ImGui::PopStyleColor(2);
            if (tabClicked)
            {
                node.activePanel = entry->info.id;
            }
            std::string popupId = "DockTabContext##" + entry->info.id;
            if (ImGui::BeginPopupContextItem(popupId.c_str()))
            {
                if (ImGui::MenuItem("Float")) floatActive = true;
                if (ImGui::MenuItem("Close")) closePanel = entry->info.id;
                ImGui::EndPopup();
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
            {
                draggedPanel = entry->info.id;
                ImGui::SetDragDropPayload(PanelDragPayload, entry->info.id.c_str(), entry->info.id.size() + 1);
                ImGui::TextUnformatted(entry->info.title.c_str());
                ImGui::EndDragDropSource();
            }
            drewTab = true;
        }
        ImGui::PopStyleVar();
        ImGui::Separator();

        PanelEntry* active = FindPanel(node.activePanel.c_str());
        if (open && active && active->visible && active->panel)
        {
            active->panel->DrawPanel();
        }
    }
    ImGui::EndChild();
    if (!node.tabs.empty()) ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    PanelDockPlacement rootPlacement = PanelDockPlacement::Center;
    if (viewport)
    {
        rootPlacement = GetDockPlacement(ToVector2(viewport->WorkPos), ToVector2(viewport->WorkSize), 0.12f);
    }
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool mergeOnTabBar = !node.tabs.empty()
        && mouse.x >= position.x
        && mouse.x <= position.x + size.x
        && mouse.y >= position.y
        && mouse.y <= position.y + DockTabHeight + 8.0f;
    const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
    if (mergeOnTabBar && activePayload && activePayload->IsDataType(PanelDragPayload)) tabMergeTargetHovered = true;

    if ((mergeOnTabBar || !IsRootDockPlacement(rootPlacement)) && ImGui::BeginDragDropTarget())
    {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(PanelDragPayload,
            ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (payload)
        {
            PanelDockPlacement placement = mergeOnTabBar
                ? PanelDockPlacement::Center
                : GetDockPlacement(position, size, 0.28f);
            DrawDockPreview(position, size, placement);
            if (payload->IsDelivery())
            {
                pendingDock.pending = true;
                pendingDock.panelId = static_cast<const char*>(payload->Data);
                pendingDock.targetNode = node.id;
                pendingDock.placement = placement;
                draggedPanel.clear();
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (!closePanel.empty()) pendingClosePanel = closePanel;
    if (floatActive && !node.activePanel.empty())
    {
        PanelEntry* entry = FindPanel(node.activePanel.c_str());
        if (entry)
        {
            pendingFloat.pending = true;
            pendingFloat.panelId = entry->info.id;
            pendingFloat.position = { position.x + 30.0f, position.y + 30.0f };
        }
    }
}

//根据鼠标到矩形四边的距离选择停靠方向。
PanelDockPlacement PanelManager::GetDockPlacement(const vector2& position, const vector2& size, float32 edgeRatio) const
{
    if (size.x <= 0.0f || size.y <= 0.0f) return PanelDockPlacement::Center;

    float32 x = std::clamp((ImGui::GetIO().MousePos.x - position.x) / size.x, 0.0f, 1.0f);
    float32 y = std::clamp((ImGui::GetIO().MousePos.y - position.y) / size.y, 0.0f, 1.0f);
    float32 left = x;
    float32 right = 1.0f - x;
    float32 top = y;
    float32 bottom = 1.0f - y;
    float32 nearest = std::min(std::min(left, right), std::min(top, bottom));
    if (nearest >= edgeRatio) return PanelDockPlacement::Center;
    if (nearest == left) return PanelDockPlacement::Left;
    if (nearest == right) return PanelDockPlacement::Right;
    if (nearest == top) return PanelDockPlacement::Top;
    return PanelDockPlacement::Bottom;
}

//绘制即将生成的 Dock 区域预览。
void PanelManager::DrawDockPreview(const vector2& position, const vector2& size, PanelDockPlacement placement) const
{
    ImVec2 min(position.x, position.y);
    ImVec2 max(position.x + size.x, position.y + size.y);
    constexpr float32 previewRatio = 0.35f;
    switch (placement)
    {
        case PanelDockPlacement::Left: max.x = min.x + size.x * previewRatio; break;
        case PanelDockPlacement::Right: min.x = max.x - size.x * previewRatio; break;
        case PanelDockPlacement::Top: max.y = min.y + size.y * previewRatio; break;
        case PanelDockPlacement::Bottom: min.y = max.y - size.y * previewRatio; break;
        case PanelDockPlacement::Center:
            min.x += size.x * 0.18f;
            min.y += size.y * 0.18f;
            max.x -= size.x * 0.18f;
            max.y -= size.y * 0.18f;
            break;
        default: return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImU32 fill = ImGui::GetColorU32(ImVec4(0.18f, 0.48f, 0.88f, 0.28f));
    ImU32 border = ImGui::GetColorU32(ImVec4(0.35f, 0.68f, 1.0f, 0.95f));
    drawList->AddRectFilled(min, max, fill, 3.0f);
    drawList->AddRect(min, max, border, 3.0f, 0, 2.0f);
    if (placement == PanelDockPlacement::Center)
    {
        const char* label = "Merge as Tab";
        ImVec2 textSize = ImGui::CalcTextSize(label);
        drawList->AddText(ImVec2((min.x + max.x - textSize.x) * 0.5f, (min.y + max.y - textSize.y) * 0.5f), border, label);
    }
}

bool PanelManager::IsRootDockPlacement(PanelDockPlacement placement) const
{
    return placement == PanelDockPlacement::Left
        || placement == PanelDockPlacement::Right
        || placement == PanelDockPlacement::Top
        || placement == PanelDockPlacement::Bottom;
}

//在 Dock 树绘制完成后统一执行结构修改，避免递归绘制期间节点地址失效。
void PanelManager::ApplyPendingCommands()
{
    if (pendingDock.pending)
    {
        DockPanel(pendingDock.panelId, pendingDock.targetNode, pendingDock.placement);
        pendingDock = PendingDockCommand();
    }

    if (pendingFloat.pending)
    {
        PanelEntry* entry = FindPanel(pendingFloat.panelId.c_str());
        if (entry)
        {
            int32 oldNode = entry->dockNode;
            RemovePanelFromDock(entry->info.id);
            CompactDockNode(oldNode);
            entry->hasPosition = true;
            entry->position = pendingFloat.position;
            entry->applyPosition = true;
        }
        pendingFloat = PendingFloatCommand();
    }


    if (!pendingClosePanel.empty())
    {
        std::string panelId = pendingClosePanel;
        pendingClosePanel.clear();
        SetPanelVisible(panelId.c_str(), false);
    }
}

//绘制未停靠的传统浮动窗口。
void PanelManager::DrawFloatingPanel(PanelEntry& entry)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return;

    ImGuiIO& io = ImGui::GetIO();
    if (entry.moving)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            entry.position = {
                io.MousePos.x - entry.moveOffset.x,
                io.MousePos.y - entry.moveOffset.y
            };
            entry.hasPosition = true;
        }
        else
        {
            entry.moving = false;
        }
    }

    ClampPanel(entry);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(std::min(MinPanelWidth, viewport->WorkSize.x), std::min(MinPanelHeight, viewport->WorkSize.y)),
        viewport->WorkSize);
    ImGui::SetNextWindowSize(ToImVec2(entry.hasSize ? entry.size : entry.info.defaultSize),
        entry.applySize ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    if (entry.hasPosition) ImGui::SetNextWindowPos(ToImVec2(entry.position), ImGuiCond_Always);

    bool visible = entry.visible;
    std::string windowTitle = entry.info.title + "###Panel" + entry.info.id;
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse;
    bool open = ImGui::Begin(windowTitle.c_str(), &visible, flags);
    vector2 position = ToVector2(ImGui::GetWindowPos());
    vector2 size = ToVector2(ImGui::GetWindowSize());
    float32 titleHeight = ImGui::GetFrameHeight();
    bool titleHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
        && io.MousePos.x >= position.x
        && io.MousePos.x <= position.x + size.x
        && io.MousePos.y >= position.y
        && io.MousePos.y <= position.y + titleHeight;
    if (titleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        entry.moving = true;
        entry.moveOffset = { io.MousePos.x - position.x, io.MousePos.y - position.y };
    }
    if (open)
    {
        std::string dockButtonId = "Dock###FloatDock" + entry.info.id;
        if (ImGui::SmallButton(dockButtonId.c_str()))
        {
            int32 target = FindBestDockTarget(dockRoot);
            if (target >= 0) DockPanel(entry.info.id, target, PanelDockPlacement::Center);
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
        {
            draggedPanel = entry.info.id;
            ImGui::SetDragDropPayload(PanelDragPayload, entry.info.id.c_str(), entry.info.id.size() + 1);
            ImGui::Text("Dock %s", entry.info.title.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Drag Dock into a panel to merge");
        ImGui::Separator();
        entry.panel->DrawPanel();
    }
    position = ToVector2(ImGui::GetWindowPos());
    size = ToVector2(ImGui::GetWindowSize());
    ClampPanelRect(position, size);
    entry.hasPosition = true;
    entry.hasSize = true;
    entry.position = position;
    entry.size = size;
    entry.applyPosition = false;
    entry.applySize = false;
    ImGui::End();
    if (visible != entry.visible)
    {
        entry.moving = false;
        ApplyVisibility(entry, visible);
    }
}

//将面板放入标签区或在目标边缘创建新分区。
void PanelManager::DockPanel(const std::string& panelId, int32 targetNodeId, PanelDockPlacement placement)
{
    PanelEntry* entry = FindPanel(panelId.c_str());
    DockNode* target = FindDockNode(targetNodeId);
    if (!entry || !target || placement == PanelDockPlacement::Floating) return;

    bool targetIsLeaf = target->firstChild < 0 && target->secondChild < 0;
    if (placement == PanelDockPlacement::Center && !targetIsLeaf)
    {
        targetNodeId = FindBestDockTarget(targetNodeId);
        target = FindDockNode(targetNodeId);
        if (!target) return;
    }

    if (entry->dockNode == targetNodeId && placement == PanelDockPlacement::Center)
    {
        target->activePanel = panelId;
        return;
    }
    if (entry->dockNode == targetNodeId && placement != PanelDockPlacement::Center && target->tabs.size() == 1)
    {
        return;
    }

    int32 oldNode = entry->dockNode;
    RemovePanelFromDock(panelId);
    target = FindDockNode(targetNodeId);
    if (!target) return;

    if (placement == PanelDockPlacement::Center)
    {
        if (std::find(target->tabs.begin(), target->tabs.end(), panelId) == target->tabs.end()) target->tabs.push_back(panelId);
        target->activePanel = panelId;
        entry->dockNode = targetNodeId;
        if (oldNode >= 0 && oldNode != targetNodeId) CompactDockNode(oldNode);
        return;
    }

    DockNode oldContent = *target;
    oldContent.id = nextDockNodeId++;
    DockNode newContent;
    newContent.id = nextDockNodeId++;
    newContent.tabs.push_back(panelId);
    newContent.activePanel = panelId;
    dockNodes.push_back(oldContent);
    dockNodes.push_back(newContent);

    target = FindDockNode(targetNodeId);
    target->tabs.clear();
    target->activePanel.clear();
    target->workspace = false;
    target->vertical = placement == PanelDockPlacement::Left || placement == PanelDockPlacement::Right;
    target->ratio = std::clamp(entry->info.defaultDockRatio, 0.1f, 0.9f);
    bool newFirst = placement == PanelDockPlacement::Left || placement == PanelDockPlacement::Top;
    if (placement == PanelDockPlacement::Right || placement == PanelDockPlacement::Bottom) target->ratio = 1.0f - target->ratio;
    target->firstChild = newFirst ? newContent.id : oldContent.id;
    target->secondChild = newFirst ? oldContent.id : newContent.id;
    entry->dockNode = newContent.id;
    for (const std::string& tab : oldContent.tabs)
    {
        if (PanelEntry* oldEntry = FindPanel(tab.c_str())) oldEntry->dockNode = oldContent.id;
    }
    if (oldNode >= 0 && oldNode != targetNodeId) CompactDockNode(oldNode);
}

void PanelManager::RemovePanelFromDock(const std::string& panelId)
{
    PanelEntry* entry = FindPanel(panelId.c_str());
    if (!entry || entry->dockNode < 0) return;
    int32 nodeId = entry->dockNode;
    if (DockNode* node = FindDockNode(nodeId))
    {
        node->tabs.erase(std::remove(node->tabs.begin(), node->tabs.end(), panelId), node->tabs.end());
        if (node->activePanel == panelId) node->activePanel = node->tabs.empty() ? std::string() : node->tabs.front();
    }
    entry->dockNode = -1;
}

//删除空叶子并把兄弟节点提升到父节点。
void PanelManager::CompactDockNode(int32 nodeId)
{
    int32 emptyNodeId = nodeId;
    while (emptyNodeId != dockRoot)
    {
        DockNode* node = FindDockNode(emptyNodeId);
        if (!node || node->workspace || !node->tabs.empty() || node->firstChild >= 0) return;

        int32 parentId = FindDockParent(emptyNodeId);
        DockNode* parent = FindDockNode(parentId);
        if (!parent) return;
        int32 siblingId = parent->firstChild == emptyNodeId ? parent->secondChild : parent->firstChild;
        DockNode* sibling = FindDockNode(siblingId);
        if (!sibling) return;

        DockNode promoted = *sibling;
        promoted.id = parentId;
        *parent = promoted;
        for (PanelEntry& entry : panels)
        {
            if (entry.dockNode == siblingId) entry.dockNode = parentId;
        }
        dockNodes.erase(std::remove_if(dockNodes.begin(), dockNodes.end(), [emptyNodeId, siblingId](const DockNode& value)
            { return value.id == emptyNodeId || value.id == siblingId; }), dockNodes.end());
        emptyNodeId = parentId;
    }
}

int32 PanelManager::FindDockParent(int32 nodeId) const
{
    for (const DockNode& node : dockNodes)
    {
        if (node.firstChild == nodeId || node.secondChild == nodeId) return node.id;
    }
    return -1;
}

void PanelManager::SynchronizeDockAssignments()
{
    for (PanelEntry& entry : panels) entry.dockNode = -1;
    for (DockNode& node : dockNodes)
    {
        node.tabs.erase(std::remove_if(node.tabs.begin(), node.tabs.end(), [this, &node](const std::string& panelId)
            {
                PanelEntry* entry = FindPanel(panelId.c_str());
                if (!entry || entry->dockNode >= 0) return true;
                entry->dockNode = node.id;
                return false;
            }), node.tabs.end());
        if (!node.tabs.empty() && std::find(node.tabs.begin(), node.tabs.end(), node.activePanel) == node.tabs.end())
        {
            node.activePanel = node.tabs.front();
        }
    }
}

//优先寻找空的中央叶子，否则返回第一个可停靠叶子。
int32 PanelManager::FindBestDockTarget(int32 nodeId) const
{
    const DockNode* node = FindDockNode(nodeId);
    if (!node) return -1;
    if (node->firstChild < 0 || node->secondChild < 0) return node->id;

    int32 first = FindBestDockTarget(node->firstChild);
    int32 second = FindBestDockTarget(node->secondChild);
    const DockNode* firstNode = FindDockNode(first);
    const DockNode* secondNode = FindDockNode(second);
    if (firstNode && firstNode->workspace) return first;
    if (secondNode && secondNode->workspace) return second;
    if (firstNode && firstNode->tabs.empty()) return first;
    if (secondNode && secondNode->tabs.empty()) return second;
    return first >= 0 ? first : second;
}
