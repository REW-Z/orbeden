#pragma once

#include "Editor/Panels/IEditorPanel.h"
#include "Defines/types.h"
#include "Runtime/EnsId.h"

class EditorScene;
class EditorSystem;
struct EnsId;
class World;

//Ens视图面板，显示当前World中的空间层级树。
class EnsViewPanel : public IEditorPanel
{
private:
    struct PendingMove
    {
    public:
        bool pending = false;
        EnsId child;
        EnsId parent;
        EnsId beforeSibling;
    };

    EditorSystem& editor;
    EditorPanelInfo info;
    PendingMove pendingMove;

public:
    explicit EnsViewPanel(EditorSystem& owner);

    //获取面板信息
    const EditorPanelInfo& GetPanelInfo() const override;

    //绘制面板内容
    void DrawPanel() override;

private:
    //绘制单个Ens节点
    void DrawEnsNode(World& world, EditorScene& sceneEditor, EnsId ens, const List<EnsId>& roots);

    //处理节点上的拖拽投放区域
    void DrawNodeDropTarget(World& world, EnsId target, const List<EnsId>& roots);

    //绘制移动到根级末尾的投放区域
    void DrawRootDropTarget(World& world);

    //判断移动命令是否有效
    bool CanMoveEns(World& world, EnsId child, EnsId parent, EnsId beforeSibling) const;

    //应用本帧排队的层级移动
    void ApplyPendingMove(World& world);
};
