#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "Editor/Panels/EnsViewPanel.h"
#include "Editor/EditorScene.h"
#include "Editor/EditorSystem.h"
#include "Editor/Panels/EditorPanelRegistry.h"
#include "Rendering/RenderMath.h"
#include "Runtime/Ens.h"
#include "Runtime/EnsId.h"
#include "Runtime/Object/SpaceComponent.h"
#include "Runtime/World.h"

namespace
{
    constexpr const char* EnsDragPayload = "ORBEDEN_ENS";

    enum class NodeDropPlacement
    {
        Before,
        Child,
        After,
    };

    //读取变换矩阵中一个轴的长度。
    float32 GetAxisLength(const matrix4x4& matrix, int32 offset)
    {
        float32 x = matrix.m[offset];
        float32 y = matrix.m[offset + 1];
        float32 z = matrix.m[offset + 2];
        return std::sqrt(x * x + y * y + z * z);
    }

    //把旋转矩阵转换为单位四元数。
    quaternion GetRotation(const matrix4x4& matrix, const vector3& scale)
    {
        float32 xScale = std::abs(scale.x) > 0.000001f ? scale.x : 1.0f;
        float32 yScale = std::abs(scale.y) > 0.000001f ? scale.y : 1.0f;
        float32 zScale = std::abs(scale.z) > 0.000001f ? scale.z : 1.0f;
        float32 m00 = matrix.m[0] / xScale;
        float32 m01 = matrix.m[4] / yScale;
        float32 m02 = matrix.m[8] / zScale;
        float32 m10 = matrix.m[1] / xScale;
        float32 m11 = matrix.m[5] / yScale;
        float32 m12 = matrix.m[9] / zScale;
        float32 m20 = matrix.m[2] / xScale;
        float32 m21 = matrix.m[6] / yScale;
        float32 m22 = matrix.m[10] / zScale;

        quaternion result;
        float32 trace = m00 + m11 + m22;
        if (trace > 0.0f)
        {
            float32 value = std::sqrt(trace + 1.0f) * 2.0f;
            result.w = 0.25f * value;
            result.x = (m21 - m12) / value;
            result.y = (m02 - m20) / value;
            result.z = (m10 - m01) / value;
        }
        else if (m00 > m11 && m00 > m22)
        {
            float32 value = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            result.w = (m21 - m12) / value;
            result.x = 0.25f * value;
            result.y = (m01 + m10) / value;
            result.z = (m02 + m20) / value;
        }
        else if (m11 > m22)
        {
            float32 value = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            result.w = (m02 - m20) / value;
            result.x = (m01 + m10) / value;
            result.y = 0.25f * value;
            result.z = (m12 + m21) / value;
        }
        else
        {
            float32 value = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            result.w = (m10 - m01) / value;
            result.x = (m02 + m20) / value;
            result.y = (m12 + m21) / value;
            result.z = 0.25f * value;
        }

        float32 length = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z + result.w * result.w);
        if (length <= 0.000001f) return quaternion();
        result.x /= length;
        result.y /= length;
        result.z /= length;
        result.w /= length;
        return result;
    }

    //把局部矩阵分解回SpaceComponent使用的TRS字段。
    void DecomposeTransform(const matrix4x4& matrix, vector3& position, quaternion& rotation, vector3& scale)
    {
        position = RenderMath::GetTranslation(matrix);
        scale = { GetAxisLength(matrix, 0), GetAxisLength(matrix, 4), GetAxisLength(matrix, 8) };

        vector3 xAxis = { matrix.m[0], matrix.m[1], matrix.m[2] };
        vector3 yAxis = { matrix.m[4], matrix.m[5], matrix.m[6] };
        vector3 zAxis = { matrix.m[8], matrix.m[9], matrix.m[10] };
        if (RenderMath::Dot(RenderMath::Cross(xAxis, yAxis), zAxis) < 0.0f) scale.x = -scale.x;
        rotation = GetRotation(matrix, scale);
    }

    //从拖拽载荷读取EnsId。
    bool TryReadDraggedEns(const ImGuiPayload* payload, EnsId& ens)
    {
        if (!payload || payload->DataSize != sizeof(EnsId) || !payload->Data) return false;
        std::memcpy(&ens, payload->Data, sizeof(EnsId));
        return true;
    }
}

EnsViewPanel::EnsViewPanel(EditorSystem& owner)
    : editor(owner)
{
    info.id = "ens_view";
    info.title = "EnsView";
    info.defaultVisible = true;
    info.defaultSize = { 320.0f, 420.0f };
    info.defaultDock = PanelDockPlacement::Left;
    info.defaultDockRatio = 0.22f;
    info.order = 200;
}

//获取面板信息
const EditorPanelInfo& EnsViewPanel::GetPanelInfo() const
{
    return info;
}

//绘制面板内容
void EnsViewPanel::DrawPanel()
{
    World& world = editor.GetWorld();
    EditorScene& sceneEditor = editor.GetEditorScene();
    ImVec2 contentMin = ImGui::GetCursorScreenPos();

    sceneEditor.PruneSelection(world);

    List<EnsId> roots;
    world.ForEachEns([&roots, &sceneEditor](Ens& ens)
    {
        if (sceneEditor.IsTemporaryEns(ens.GetId())) return;

        Ens* parent = ens.GetParent();
        if (!parent)
        {
            roots.push_back(ens.GetId());
        }
    });

    if (roots.empty())
    {
        ImGui::TextUnformatted("No Ens objects.");
    }
    else
    {
        for (EnsId root : roots)
        {
            DrawEnsNode(world, sceneEditor, root, roots);
        }
    }

    DrawRootDropTarget(world);

    //普通点击面板空白时清空选择。
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl
        && io.MousePos.y >= contentMin.y
        && ImGui::IsWindowHovered()
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !ImGui::IsAnyItemHovered())
    {
        sceneEditor.ClearSelection();
    }

    ApplyPendingMove(world);
}

//绘制单个Ens节点
void EnsViewPanel::DrawEnsNode(World& world, EditorScene& sceneEditor, EnsId ens, const List<EnsId>& roots)
{
    if (!world.IsAlive(ens)) return;
    if (sceneEditor.IsTemporaryEns(ens)) return;

    Ens* ensObject = world.GetEns(ens);
    if (!ensObject) return;

    SpaceComponent* space = world.GetSpaceComponent(ens);
    bool hasChildren = space && !space->firstChild.IsNull();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (hasChildren)
    {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    if (sceneEditor.IsSelected(ens))
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!hasChildren)
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    std::string label = ensObject->GetName();
    label += "##";
    label += std::to_string(ens.id);
    label += "_";
    label += std::to_string(ens.version);

    bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        if (ImGui::GetIO().KeyCtrl) sceneEditor.ToggleEns(ens);
        else sceneEditor.SelectEns(ens);
    }

    DrawNodeDropTarget(world, ens, roots);
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload(EnsDragPayload, &ens, sizeof(ens));
        ImGui::Text("Move %s", ensObject->GetName().c_str());
        ImGui::EndDragDropSource();
    }

    if (open && hasChildren)
    {
        EnsId child = space->firstChild;
        while (!child.IsNull())
        {
            DrawEnsNode(world, sceneEditor, child, roots);

            SpaceComponent* childSpace = world.GetSpaceComponent(child);
            child = childSpace ? childSpace->next : EnsId();
        }

        ImGui::TreePop();
    }
}

//处理节点上的拖拽投放区域
void EnsViewPanel::DrawNodeDropTarget(World& world, EnsId target, const List<EnsId>& roots)
{
    ImVec2 itemMin = ImGui::GetItemRectMin();
    ImVec2 itemMax = ImGui::GetItemRectMax();
    if (!ImGui::BeginDragDropTarget()) return;

    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EnsDragPayload,
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
    EnsId child;
    if (TryReadDraggedEns(payload, child))
    {
        float32 height = std::max(1.0f, itemMax.y - itemMin.y);
        float32 ratio = std::clamp((ImGui::GetIO().MousePos.y - itemMin.y) / height, 0.0f, 1.0f);
        NodeDropPlacement placement = ratio < 0.25f
            ? NodeDropPlacement::Before
            : ratio > 0.75f ? NodeDropPlacement::After : NodeDropPlacement::Child;

        SpaceComponent* targetSpace = world.GetSpaceComponent(target);
        EnsId parent;
        EnsId beforeSibling;
        if (placement == NodeDropPlacement::Child)
        {
            parent = target;
        }
        else if (targetSpace)
        {
            parent = targetSpace->parent;
            if (placement == NodeDropPlacement::Before)
            {
                beforeSibling = target;
            }
            else if (!parent.IsNull())
            {
                beforeSibling = targetSpace->next;
            }
            else
            {
                auto root = std::find(roots.begin(), roots.end(), target);
                if (root != roots.end() && ++root != roots.end()) beforeSibling = *root;
            }
        }

        bool valid = CanMoveEns(world, child, parent, beforeSibling);
        ImU32 color = valid ? IM_COL32(65, 160, 255, 255) : IM_COL32(220, 75, 75, 255);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (placement == NodeDropPlacement::Child)
        {
            drawList->AddRect(itemMin, itemMax, color, 3.0f, 0, 2.0f);
        }
        else
        {
            float32 y = placement == NodeDropPlacement::Before ? itemMin.y : itemMax.y;
            drawList->AddLine(ImVec2(itemMin.x, y), ImVec2(itemMax.x, y), color, 2.0f);
        }

        if (valid && payload->IsDelivery())
        {
            pendingMove = { true, child, parent, beforeSibling };
        }
    }

    ImGui::EndDragDropTarget();
}

//绘制移动到根级末尾的投放区域
void EnsViewPanel::DrawRootDropTarget(World& world)
{
    const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
    if (!activePayload || !activePayload->IsDataType(EnsDragPayload)) return;

    ImVec2 position = ImGui::GetCursorScreenPos();
    ImVec2 available = ImGui::GetContentRegionAvail();
    ImVec2 size = { std::max(1.0f, available.x), std::max(28.0f, available.y) };
    ImGui::InvisibleButton("##ens_root_drop_target", size);
    ImVec2 max = { position.x + size.x, position.y + size.y };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(position, max, IM_COL32(90, 110, 135, 180), 4.0f, 0, 1.0f);
    drawList->AddText(ImVec2(position.x + 8.0f, position.y + 6.0f), IM_COL32(170, 185, 205, 255), "Drop here to move to root");
    if (!ImGui::BeginDragDropTarget()) return;

    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EnsDragPayload,
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
    EnsId child;
    if (TryReadDraggedEns(payload, child))
    {
        bool valid = CanMoveEns(world, child, EnsId(), EnsId());
        ImU32 color = valid ? IM_COL32(65, 160, 255, 255) : IM_COL32(220, 75, 75, 255);
        drawList->AddRect(position, max, color, 4.0f, 0, 2.0f);
        if (valid && payload->IsDelivery()) pendingMove = { true, child, EnsId(), EnsId() };
    }
    ImGui::EndDragDropTarget();
}

//判断移动命令是否有效
bool EnsViewPanel::CanMoveEns(World& world, EnsId child, EnsId parent, EnsId beforeSibling) const
{
    const EditorScene& sceneEditor = editor.GetEditorScene();
    if (!world.IsAlive(child) || child == parent || child == beforeSibling) return false;
    if (sceneEditor.IsTemporaryEns(child)) return false;
    if (!parent.IsNull() && (!world.IsAlive(parent) || sceneEditor.IsTemporaryEns(parent))) return false;

    if (!beforeSibling.IsNull())
    {
        SpaceComponent* beforeSpace = world.GetSpaceComponent(beforeSibling);
        if (!beforeSpace || beforeSpace->parent != parent || sceneEditor.IsTemporaryEns(beforeSibling)) return false;
    }

    EnsId current = parent;
    while (!current.IsNull())
    {
        if (current == child) return false;
        SpaceComponent* currentSpace = world.GetSpaceComponent(current);
        current = currentSpace ? currentSpace->parent : EnsId();
    }
    return true;
}

//应用本帧排队的层级移动
void EnsViewPanel::ApplyPendingMove(World& world)
{
    if (!pendingMove.pending) return;

    PendingMove move = pendingMove;
    pendingMove.pending = false;
    if (!CanMoveEns(world, move.child, move.parent, move.beforeSibling)) return;

    SpaceComponent* space = world.GetSpaceComponent(move.child);
    if (!space) return;

    EnsId oldParent = space->parent;
    matrix4x4 worldMatrix = space->worldMatrix;
    if (!world.MoveEns(move.child, move.parent, move.beforeSibling)) return;
    if (oldParent == move.parent) return;

    SpaceComponent* parentSpace = move.parent.IsNull() ? nullptr : world.GetSpaceComponent(move.parent);
    matrix4x4 localMatrix = parentSpace
        ? RenderMath::Mul(RenderMath::Inverse(parentSpace->worldMatrix), worldMatrix)
        : worldMatrix;
    DecomposeTransform(localMatrix, space->localPosition, space->localRotation, space->localScale);
    space->transformDirty = true;
}

ORBEDEN_REGISTER_EDITOR_PANEL(EnsViewPanel)
