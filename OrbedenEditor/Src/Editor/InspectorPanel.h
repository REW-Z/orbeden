#pragma once

#include "Editor/IEditorPanel.h"

class EditorSystem;

//托管 Inspector 面板外壳。
class InspectorPanel : public IEditorPanel
{
private:
    EditorSystem& editor;

public:
    explicit InspectorPanel(EditorSystem& owner);

    //获取面板稳定ID
    const char* GetPanelId() const override;

    //获取面板显示标题
    const char* GetPanelTitle() const override;

    //绘制面板内容
    void DrawPanel() override;
};
