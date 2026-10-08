#pragma once

#include "Rendering/RenderScene.h"

//绘制项排序
class RenderItemSorter
{
public:
    //排序相机临时绘制项
    void Sort(VisibleSet& visibleSet);
};
