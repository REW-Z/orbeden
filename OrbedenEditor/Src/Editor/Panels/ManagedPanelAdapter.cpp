#include "Editor/Panels/ManagedPanelAdapter.h"

#include "Editor/EditorSystem.h"

#include <utility>

ManagedPanelAdapter::ManagedPanelAdapter(EditorSystem& owner, EditorPanelInfo panelInfo, int32 panelHandle)
    : editor(owner)
    , info(std::move(panelInfo))
    , handle(panelHandle)
{
}

const EditorPanelInfo& ManagedPanelAdapter::GetPanelInfo() const
{
    return info;
}

void ManagedPanelAdapter::DrawPanel()
{
    editor.DrawManagedPanel(handle);
}

void ManagedPanelAdapter::OnPanelShown()
{
    editor.SetManagedPanelVisible(handle, true);
}

void ManagedPanelAdapter::OnPanelHidden()
{
    editor.SetManagedPanelVisible(handle, false);
}
