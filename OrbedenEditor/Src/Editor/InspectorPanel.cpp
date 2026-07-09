#include "Editor/InspectorPanel.h"

#include "Editor/EditorSystem.h"

InspectorPanel::InspectorPanel(EditorSystem& owner)
    : editor(owner)
{
}

//获取面板稳定ID
const char* InspectorPanel::GetPanelId() const
{
    return "inspector";
}

//获取面板显示标题
const char* InspectorPanel::GetPanelTitle() const
{
    return "Inspector";
}

//绘制面板内容
void InspectorPanel::DrawPanel()
{
    editor.DrawManagedInspectorContent();
}
