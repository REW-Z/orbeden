#pragma once

#include "Editor/Panels/IEditorPanel.h"

class EditorSystem;

//把一个托管 Panel 实例接入原生 PanelManager。
class ManagedPanelAdapter final : public IEditorPanel
{
private:
    EditorSystem& editor;
    EditorPanelInfo info;
    int32 handle = -1;

public:
    ManagedPanelAdapter(EditorSystem& owner, EditorPanelInfo panelInfo, int32 panelHandle);

    //获取面板信息
    const EditorPanelInfo& GetPanelInfo() const override;

    //绘制托管面板
    void DrawPanel() override;

    //通知托管面板已经显示
    void OnPanelShown() override;

    //通知托管面板已经隐藏
    void OnPanelHidden() override;
};
