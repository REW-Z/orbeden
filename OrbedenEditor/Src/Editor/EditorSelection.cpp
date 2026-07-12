#include "Editor/EditorSelection.h"

#include "Runtime/World.h"

#include <algorithm>

//选择一个Ens
void EditorSelection::SelectEns(EnsId ens)
{
    selectedEns.clear();
    activeEns = ens;
    if (!ens.IsNull()) selectedEns.push_back(ens);
}

//切换一个Ens的选择状态
void EditorSelection::ToggleEns(EnsId ens)
{
    if (ens.IsNull()) return;

    auto it = std::find(selectedEns.begin(), selectedEns.end(), ens);
    if (it == selectedEns.end())
    {
        selectedEns.push_back(ens);
        activeEns = ens;
        return;
    }

    bool removedActive = activeEns == ens;
    selectedEns.erase(it);
    if (removedActive)
    {
        activeEns = selectedEns.empty() ? EnsId() : selectedEns.back();
    }
}

//清空当前选择
void EditorSelection::Clear()
{
    selectedEns.clear();
    activeEns = EnsId();
}

//获取当前选中的Ens
EnsId EditorSelection::GetSelectedEns() const
{
    return activeEns;
}

//判断指定Ens是否被选中
bool EditorSelection::IsSelected(EnsId ens) const
{
    return std::find(selectedEns.begin(), selectedEns.end(), ens) != selectedEns.end();
}

//获取全部选中的Ens
const List<EnsId>& EditorSelection::GetSelectedEnsList() const
{
    return selectedEns;
}

//移除已经失效的Ens
void EditorSelection::PruneInvalid(const World& world)
{
    selectedEns.erase(std::remove_if(selectedEns.begin(), selectedEns.end(), [&world](EnsId ens)
    {
        return !world.IsAlive(ens);
    }), selectedEns.end());

    if (std::find(selectedEns.begin(), selectedEns.end(), activeEns) == selectedEns.end())
    {
        activeEns = selectedEns.empty() ? EnsId() : selectedEns.back();
    }
}
