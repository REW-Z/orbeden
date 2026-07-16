#include "Editor/Panels/EditorPanelRegistry.h"

#include "Editor/EditorSystem.h"

namespace
{
    //获取进程内面板工厂列表
    std::vector<EditorPanelFactory>& GetPanelFactories()
    {
        static std::vector<EditorPanelFactory> factories;
        return factories;
    }
}

void EditorPanelRegistry::RegisterFactory(EditorPanelFactory factory)
{
    if (factory) GetPanelFactories().push_back(factory);
}

std::vector<std::unique_ptr<IEditorPanel>> EditorPanelRegistry::CreatePanels(EditorSystem& editor)
{
    std::vector<std::unique_ptr<IEditorPanel>> panels;
    panels.reserve(GetPanelFactories().size());
    for (EditorPanelFactory factory : GetPanelFactories())
    {
        panels.push_back(factory(editor));
    }
    return panels;
}
