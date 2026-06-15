#pragma once

#include "Runtime/EnsId.h"

//编辑器全局选择状态。
class EditorSelection
{
private:
    EnsId selectedEns;

public:
    //选择一个Ens
    void SelectEns(EnsId ens);

    //清空当前选择
    void Clear();

    //获取当前选中的Ens
    EnsId GetSelectedEns() const;

    //判断指定Ens是否被选中
    bool IsSelected(EnsId ens) const;
};
