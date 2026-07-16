#pragma once

#include "Editor/Panels/IEditorPanel.h"

class EditorSystem;

//项目面板，显示当前项目状态和常用项目操作。
class ProjectPanel : public IEditorPanel
{
private:
    EditorSystem& editor;
    EditorPanelInfo info;

public:
    explicit ProjectPanel(EditorSystem& owner);

    //获取面板信息
    const EditorPanelInfo& GetPanelInfo() const override;

    //绘制面板内容
    void DrawPanel() override;
};
