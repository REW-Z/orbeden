#pragma once

#include "Editor/Panels/IEditorPanel.h"

class EditorSystem;

//游戏构建面板，显示当前项目状态和构建操作。
class BuildGamePanel : public IEditorPanel
{
private:
    EditorSystem& editor;
    EditorPanelInfo info;

public:
    explicit BuildGamePanel(EditorSystem& owner);

    //获取面板信息
    const EditorPanelInfo& GetPanelInfo() const override;

    //绘制面板内容
    void DrawPanel() override;
};
