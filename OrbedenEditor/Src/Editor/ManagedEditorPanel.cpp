#include "Editor/ManagedEditorPanel.h"

#include "Editor/EditorSystem.h"

ManagedEditorPanel::ManagedEditorPanel(EditorSystem& owner)
    : editor(owner)
{
}

//获取面板稳定ID
const char* ManagedEditorPanel::GetPanelId() const
{
    return "managed_editor";
}

//获取面板显示标题
const char* ManagedEditorPanel::GetPanelTitle() const
{
    return "C# Editor Panel";
}

//绘制面板内容
void ManagedEditorPanel::DrawPanel()
{
    editor.DrawManagedEditorPanelContent();
}
