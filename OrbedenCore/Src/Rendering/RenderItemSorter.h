#pragma once

#include "Rendering/RenderScene.h"

//绘制项排序
class RenderItemSorter
{
public:
    //排序可见项
    void Sort(VisibleSet& visibleSet);
};

