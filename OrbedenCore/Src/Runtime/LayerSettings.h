#pragma once

#include "Defines/types.h"

//项目共享的物理层矩阵，缺少配置时保持全部层互相碰撞。
class LayerSettings
{
public:
    static constexpr const char* FileName = "ProjectSettings.layers";

    //内容根或配置文件变化时重新读取矩阵。
    static void Refresh();

    //合并所属层允许碰撞的目标层，兼容历史多位 Layer。
    static uint32 GetCollisionMask(uint32 layers);
};
