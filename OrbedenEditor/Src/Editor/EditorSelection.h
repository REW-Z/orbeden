#pragma once

#include "Runtime/EnsId.h"

class World;

//编辑器全局选择状态。
class EditorSelection
{
private:
    List<EnsId> selectedEns;
    EnsId activeEns;

public:
    //选择一个Ens
    void SelectEns(EnsId ens);

    /// <summary>切换一个Ens的选择状态。</summary>
    void ToggleEns(EnsId ens);

    //清空当前选择
    void Clear();

    //获取当前选中的Ens
    EnsId GetSelectedEns() const;

    //判断指定Ens是否被选中
    bool IsSelected(EnsId ens) const;

    /// <summary>获取全部选中的Ens。</summary>
    const List<EnsId>& GetSelectedEnsList() const;

    /// <summary>移除已经失效的Ens。</summary>
    void PruneInvalid(const World& world);
};
