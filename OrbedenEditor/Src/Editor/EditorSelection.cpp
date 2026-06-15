#include "Editor/EditorSelection.h"

//选择一个Ens
void EditorSelection::SelectEns(EnsId ens)
{
    selectedEns = ens;
}

//清空当前选择
void EditorSelection::Clear()
{
    selectedEns = EnsId();
}

//获取当前选中的Ens
EnsId EditorSelection::GetSelectedEns() const
{
    return selectedEns;
}

//判断指定Ens是否被选中
bool EditorSelection::IsSelected(EnsId ens) const
{
    return selectedEns == ens;
}
