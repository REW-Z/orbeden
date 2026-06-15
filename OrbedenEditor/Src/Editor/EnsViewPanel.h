#pragma once

#include "Editor/IEditorPanel.h"

class EditorSelection;
class EditorSystem;
struct EnsId;
class World;

//Ens视图面板，显示当前World中的空间层级树。
class EnsViewPanel : public IEditorPanel
{
private:
    EditorSystem& editor;

public:
    explicit EnsViewPanel(EditorSystem& owner);

    //获取面板稳定ID
    const char* GetPanelId() const override;

    //获取面板显示标题
    const char* GetPanelTitle() const override;

    //绘制面板内容
    void DrawPanel() override;

private:
    //绘制单个Ens节点
    void DrawEnsNode(World& world, EditorSelection& selection, EnsId ens);
};
