#pragma once

#include "Editor/IEditorPanel.h"

class EditorSystem;

//项目面板，显示当前项目状态和常用项目操作。
class ProjectPanel : public IEditorPanel
{
private:
    EditorSystem& editor;

public:
    explicit ProjectPanel(EditorSystem& owner);

    //获取面板稳定ID
    const char* GetPanelId() const override;

    //获取面板显示标题
    const char* GetPanelTitle() const override;

    //绘制面板内容
    void DrawPanel() override;
};
