#pragma once

#include "Editor/Panels/IEditorPanel.h"

#include <memory>
#include <vector>

class EditorSystem;

using EditorPanelFactory = std::unique_ptr<IEditorPanel>(*)(EditorSystem&);

//原生编辑器面板工厂注册表。
class EditorPanelRegistry
{
public:
    //注册一个面板工厂
    static void RegisterFactory(EditorPanelFactory factory);

    //创建全部已注册面板
    static std::vector<std::unique_ptr<IEditorPanel>> CreatePanels(EditorSystem& editor);
};

//注册一个面板类型
template<typename T>
class EditorPanelRegistration
{
public:
    EditorPanelRegistration()
    {
        EditorPanelRegistry::RegisterFactory([](EditorSystem& editor) -> std::unique_ptr<IEditorPanel>
        {
            return std::make_unique<T>(editor);
        });
    }
};

#define ORBEDEN_JOIN_PANEL_NAME_IMPL(a, b) a##b
#define ORBEDEN_JOIN_PANEL_NAME(a, b) ORBEDEN_JOIN_PANEL_NAME_IMPL(a, b)
#define ORBEDEN_REGISTER_EDITOR_PANEL(type) \
    namespace { EditorPanelRegistration<type> ORBEDEN_JOIN_PANEL_NAME(RegisteredEditorPanel, __COUNTER__); }
