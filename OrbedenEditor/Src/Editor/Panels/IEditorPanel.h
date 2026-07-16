#pragma once

#include "Runtime/EngineTypes.h"

#include <string>

enum class PanelDockPlacement
{
    Center,
    Left,
    Right,
    Top,
    Bottom,
    Floating
};

//面板注册和默认布局信息。
struct EditorPanelInfo
{
public:
    std::string id;
    std::string title;
    bool defaultVisible = true;
    vector2 defaultSize = { 320.0f, 240.0f };
    PanelDockPlacement defaultDock = PanelDockPlacement::Center;
    float32 defaultDockRatio = 0.25f;
    int32 order = 0;
};

//编辑器面板接口。
class IEditorPanel
{
public:
    virtual ~IEditorPanel() = default;

    //获取面板信息
    virtual const EditorPanelInfo& GetPanelInfo() const = 0;

    //绘制面板内容
    virtual void DrawPanel() = 0;

    //面板显示时调用
    virtual void OnPanelShown() {}

    //面板隐藏时调用
    virtual void OnPanelHidden() {}
};
