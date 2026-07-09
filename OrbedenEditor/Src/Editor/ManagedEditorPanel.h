#pragma once

#include "Editor/IEditorPanel.h"

class EditorSystem;

//托管 Editor 面板外壳。
class ManagedEditorPanel : public IEditorPanel
{
private:
    EditorSystem& editor;

public:
    explicit ManagedEditorPanel(EditorSystem& owner);

    //获取面板稳定ID
    const char* GetPanelId() const override;

    //获取面板显示标题
    const char* GetPanelTitle() const override;

    //绘制面板内容
    void DrawPanel() override;
};
