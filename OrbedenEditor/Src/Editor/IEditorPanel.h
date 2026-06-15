#pragma once

//编辑器浮动面板接口。
class IEditorPanel
{
public:
    virtual ~IEditorPanel() = default;

    //获取面板稳定ID
    virtual const char* GetPanelId() const = 0;

    //获取面板显示标题
    virtual const char* GetPanelTitle() const = 0;

    //绘制面板内容
    virtual void DrawPanel() = 0;

    //面板显示时调用
    virtual void OnPanelShown() {}

    //面板隐藏时调用
    virtual void OnPanelHidden() {}
};
