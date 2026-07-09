#pragma once

#include "Editor/EditorLayoutState.h"
#include "Editor/IEditorPanel.h"
#include "Runtime/EngineTypes.h"

#include <string>

//面板注册信息。
struct PanelInfo
{
public:
    const char* id = "";
    const char* title = "";
    bool defaultVisible = true;
    vector2 defaultSize = { 320.0f, 240.0f };
};

//编辑器浮动面板管理器。
class PanelManager
{
private:
    struct PanelEntry
    {
    public:
        PanelInfo info;
        std::string id;
        std::string title;
        IEditorPanel* panel = nullptr;
        bool visible = false;
        bool hasPosition = false;
        bool hasSize = false;
        bool applyPosition = false;
        bool applySize = false;
        vector2 position = { 0.0f, 0.0f };
        vector2 size = { 0.0f, 0.0f };
    };

    List<PanelEntry> panels;

public:
    //注册一个面板实例
    void RegisterPanel(const PanelInfo& info, IEditorPanel* panel);

    //绘制 Views 菜单内容
    void DrawViewsMenu();

    //绘制所有可见面板
    void DrawPanels();

    //应用项目中保存的面板布局
    void ApplyLayout(const EditorLayoutState& layout);

    //写出当前面板布局
    void WriteLayout(EditorLayoutState& layout) const;

    //判断面板是否可见
    bool IsPanelVisible(const char* id) const;

    //设置面板可见状态
    void SetPanelVisible(const char* id, bool visible);

private:
    PanelEntry* FindPanel(const char* id);
    const PanelEntry* FindPanel(const char* id) const;
    void ApplyVisibility(PanelEntry& entry, bool visible);
    void ClampPanel(PanelEntry& entry) const;
    bool ClampPanelRect(vector2& position, vector2& size) const;
};
