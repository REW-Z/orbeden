#include <imgui.h>
#include <string>

#include "Editor/EnsViewPanel.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorSystem.h"
#include "Runtime/Ens.h"
#include "Runtime/EnsId.h"
#include "Runtime/Object/SpaceComponent.h"
#include "Runtime/World.h"

EnsViewPanel::EnsViewPanel(EditorSystem& owner)
    : editor(owner)
{
}

//获取面板稳定ID
const char* EnsViewPanel::GetPanelId() const
{
    return "ens_view";
}

//获取面板显示标题
const char* EnsViewPanel::GetPanelTitle() const
{
    return "EnsView";
}

//绘制面板内容
void EnsViewPanel::DrawPanel()
{
    World& world = editor.GetWorld();
    EditorSelection& selection = editor.GetSelection();

    EnsId selectedEns = selection.GetSelectedEns();
    if (!selectedEns.IsNull() && !world.IsAlive(selectedEns))
    {
        selection.Clear();
    }

    List<EnsId> roots;
    world.ForEachEns([&roots](Ens& ens)
    {
        Ens* parent = ens.GetParent();
        if (!parent)
        {
            roots.push_back(ens.GetId());
        }
    });

    if (roots.empty())
    {
        ImGui::TextUnformatted("No Ens objects.");
        return;
    }

    for (EnsId root : roots)
    {
        DrawEnsNode(world, selection, root);
    }
}

//绘制单个Ens节点
void EnsViewPanel::DrawEnsNode(World& world, EditorSelection& selection, EnsId ens)
{
    if (!world.IsAlive(ens)) return;

    Ens* ensObject = world.GetEns(ens);
    if (!ensObject) return;

    SpaceComponent* space = world.GetSpaceComponent(ens);
    bool hasChildren = space && !space->firstChild.IsNull();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (hasChildren)
    {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    if (selection.IsSelected(ens))
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
    if (ImGui::IsItemClicked())
    {
        selection.SelectEns(ens);
    }

    if (open && hasChildren)
    {
        EnsId child = space->firstChild;
        while (!child.IsNull())
        {
            DrawEnsNode(world, selection, child);

            SpaceComponent* childSpace = world.GetSpaceComponent(child);
            child = childSpace ? childSpace->next : EnsId();
        }

        ImGui::TreePop();
    }
}
